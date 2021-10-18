/*******************************************************************************
  Copyright (c) 2016-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
  SPDX-License-Identifier: MIT

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*******************************************************************************/

#include "compat.h"

#include <sys/types.h> /* uint32_t */
#include <sys/ioctl.h> /* ioctl */
#include <sys/socket.h>/* SO_NOSIGPIPE */
#include <sys/uio.h>   /* iovec */

#include <assert.h>    /* assert */
#include <stddef.h>    /* offsetof */
#include <stdlib.h>    /* realloc */
#include <string.h>    /* memmove */

#include "sys/inotify.h"

#include "config.h"
#include "event-queue.h"
#include "utils.h"
#include "worker.h"

/**
 * Initialize resources associated with inotify event queue.
 *
 * @param[in] eq A pointer to #event_queue.
 **/
void
event_queue_init (struct event_queue *eq)
{
    eq->allocated = 0;
    eq->sb_events = 0;
    eq->mem_events = 0;
    eq->iov = NULL;
    eq->last = NULL;
    event_queue_set_max_events (eq, IN_DEF_MAX_QUEUED_EVENTS);
}

/**
 * Free resources associated with inotify event queue.
 *
 * @param[in] eq A pointer to #event_queue.
 **/
void
event_queue_free (struct event_queue *eq)
{
    int i;

    for (i = 0; i < eq->mem_events; i++) {
        free (eq->iov[i].iov_base);
    }
    free (eq->iov);
    free (eq->last);
}

/**
 * Set maximum length for inotify event queue
 *
 * @param[in] eq         A pointer to #event_queue.
 * @param[in] max_events A maximal length of queue (in events)
 * @return 0 on success, -1 otherwise.
 **/
int
event_queue_set_max_events (struct event_queue *eq, int max_events)
{
    if (max_events <= 0) {
        errno = EINVAL;
        return -1;
    }
    /* TODO: Implement event queue truncation */
    eq->max_events = max_events;
    return 0;
}

/**
 * Extend inotify event queue space by one item.
 *
 * @param[in] eq A pointer to #event_queue.
 **/
static int
event_queue_extend (struct event_queue *eq)
{
    if (eq->mem_events >= eq->allocated) {
        int to_allocate = eq->mem_events * 3 / 2;
        void *ptr;

        if (to_allocate < 10) {
            to_allocate = 10;
        }
        if (to_allocate > eq->max_events) {
            to_allocate = eq->max_events + 1;
        }

        ptr = realloc (eq->iov, sizeof (struct iovec) * to_allocate);
        if (ptr == NULL) {
            perror_msg (("Failed to extend events to %d items", to_allocate));
            return -1;
        }
        eq->iov = ptr;
        eq->allocated = to_allocate;
    }

    return 0;
}

/**
 * Place inotify event in to event queue.
 *
 * @param[in] eq     A pointer to #event_queue.
 * @param[in] wd     An associated watch's id.
 * @param[in] mask   An inotify watch mask.
 * @param[in] cookie Event cookie.
 * @param[in] name   File name (may be NULL).
 * @return 0 on success, -1 otherwise.
 **/
int
event_queue_enqueue (struct event_queue *eq,
                     int                 wd,
                     uint32_t            mask,
                     uint32_t            cookie,
                     const char         *name)
{
    struct inotify_event *prev_ie;
    int retval = 0;

    if (eq->mem_events > eq->max_events) {
        return -1;
    }

    if (event_queue_extend (eq) == -1) {
        return -1;
    }

    if (eq->mem_events == eq->max_events) {
        wd = -1;
        mask = IN_Q_OVERFLOW;
        cookie = 0;
        name = NULL;
        retval = -1;
    }

    /*
     * Find previous reported event. If event queue is not empty, get last
     * event from tail. Otherwise get last event sent to communication pipe.
     */
    prev_ie = eq->mem_events > 0 ?
        (struct inotify_event*)eq->iov[eq->mem_events - 1].iov_base : eq->last;

    /* Compare current event with previous to decide if it can be coalesced */
    if (prev_ie != NULL &&
        prev_ie->wd == wd &&
        prev_ie->mask == mask &&
        prev_ie->cookie == cookie &&
      ((prev_ie->len == 0 && name == NULL) ||
       (prev_ie->len > 0 && name != NULL && !strcmp (prev_ie->name, name)))) {

            int fd = EQ_TO_WRK(eq)->io[INOTIFY_FD];
            int buffered = 0;

            /* Events are identical and queue is not empty. Skip current. */
            if (eq->mem_events > 0) {
                return retval;
            }
            /* Event queue is empty. Check if any events remain in the pipe */
            if (ioctl (fd, FIONREAD, &buffered) == 0 && buffered > 0) {
                return retval;
            }
    }

    eq->iov[eq->mem_events].iov_base = (void *)create_inotify_event (
        wd, mask, cookie, name, &eq->iov[eq->mem_events].iov_len);
    if (eq->iov[eq->mem_events].iov_base == NULL) {
        perror_msg (("Failed to create a inotify event %x", mask));
        return -1;
    }

    ++eq->mem_events;

    return retval;
}

/**
 * Flush inotify events queue to socket
 *
 * @param[in] eq      A pointer to #event_queue.
 * @param[in] sbspace Amount of space in socket buffer available to write
 *                    w/o blocking
 * @return Number of bytes written to socket on success, -1 otherwise.
 **/
ssize_t
event_queue_flush (struct event_queue *eq, size_t sbspace)
{
    int iovcnt, iovmax;
    int send_flags = 0;
    int fd = EQ_TO_WRK(eq)->io[KQUEUE_FD];
    size_t iovlen = 0;
    ssize_t size;
    int i;

    iovmax = eq->mem_events;
    if (iovmax > IOV_MAX) {
        iovmax = IOV_MAX;
    }

    for (iovcnt = 0; iovcnt < iovmax; iovcnt++) {
        if (iovlen + eq->iov[iovcnt].iov_len > sbspace) {
            break;
        }
        iovlen += eq->iov[iovcnt].iov_len;
    }

    if (iovcnt == 0) {
        return 0;
    }

#if defined (MSG_NOSIGNAL)
    send_flags |= MSG_NOSIGNAL;
#endif

    size = sendv (fd, eq->iov, iovcnt, send_flags);
    assert (size == iovlen || size == -1);
    if (size > 0) {
        /* Save last event sent to communication pipe for coalecsing checks */
        free (eq->last);
        eq->last = (void *)eq->iov[iovcnt - 1].iov_base;

        for (i = 0; i < iovcnt - 1; i++) {
            free (eq->iov[i].iov_base);
        }

        memmove (&eq->iov[0],
                 &eq->iov[iovcnt],
                 sizeof(struct iovec) * (eq->mem_events - iovcnt));
        eq->mem_events -= iovcnt;
        eq->sb_events += iovcnt;
    } else {
        perror_msg (("Sending of inotify events to socket failed"));
    }

    return size;
}

/**
 * Remove last event sent to communication pipe from internal buffer
 *
 * @param[in] eq A pointer to #event_queue.
 **/
void
event_queue_reset_last (struct event_queue *eq)
{
    assert (eq != NULL);

    free (eq->last);
    eq->last = NULL;
    eq->sb_events = 0;
}
