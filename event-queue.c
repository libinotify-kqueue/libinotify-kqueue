/*******************************************************************************
  Copyright (c) 2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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
event_queue_init (event_queue *eq)
{
    eq->allocated = 0;
    eq->count = 0;
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
event_queue_free (event_queue *eq)
{
    int i;

    for (i = 0; i < eq->count; i++) {
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
event_queue_set_max_events (event_queue *eq, int max_events)
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
event_queue_extend (event_queue *eq)
{
    if (eq->count >= eq->allocated) {
        int to_allocate = eq->count + 1;
        void *ptr = realloc (eq->iov, sizeof (struct iovec) * to_allocate);
        if (ptr == NULL) {
            perror_msg ("Failed to extend events to %d items", to_allocate);
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
event_queue_enqueue (event_queue *eq,
                     int          wd,
                     uint32_t     mask,
                     uint32_t     cookie,
                     const char  *name)
{
    struct inotify_event *prev_ie;
    int retval = 0;

    if (eq->count > eq->max_events) {
        return -1;
    }

    if (event_queue_extend (eq) == -1) {
        return -1;
    }

    if (eq->count == eq->max_events) {
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
    prev_ie = (eq->count > 0) ? eq->iov[eq->count - 1].iov_base : eq->last;

    /* Compare current event with previous to decide if it can be coalesced */
    if (prev_ie != NULL &&
        prev_ie->wd == wd &&
        prev_ie->mask == mask &&
        prev_ie->cookie == cookie &&
      ((prev_ie->len == 0 && name == NULL) ||
       (prev_ie->len > 0 && name != NULL && !strcmp (prev_ie->name, name)))) {
            /* Events are identical and queue is not empty. Skip current. */
            if (eq->count > 0) {
                return retval;
            }
            /* Event queue is empty. Check if any events remain in the pipe */
            int fd = EQ_TO_WRK(eq)->io[INOTIFY_FD];
            int buffered;
            if (ioctl (fd, FIONREAD, &buffered) == 0 && buffered > 0) {
                return retval;
            }
    }

    eq->iov[eq->count].iov_base = create_inotify_event (
        wd, mask, cookie, name, &eq->iov[eq->count].iov_len);
    if (eq->iov[eq->count].iov_base == NULL) {
        perror_msg ("Failed to create a inotify event %x", mask);
        return -1;
    }

    ++eq->count;

    return retval;
}

/**
 * Flush inotify events queue to socket
 *
 * @param[in] eq      A pointer to #event_queue.
 * @param[in] sbspace Amount of space in socket buffer available to write
 *                    w/o blocking
 **/
void event_queue_flush (event_queue *eq, size_t sbspace)
{
    int iovcnt, iovmax;
    size_t iovlen = 0;

    iovmax = eq->count;
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
        return;
    }

    int send_flags = 0;
#if defined (MSG_NOSIGNAL)
    send_flags |= MSG_NOSIGNAL;
#endif

    int fd = EQ_TO_WRK(eq)->io[KQUEUE_FD];
    if (safe_sendv (fd, eq->iov, iovcnt, send_flags) == -1) {
        perror_msg ("Sending of inotify events to socket failed");
    }

    /* Save last event sent to communication pipe for coalecsing checks */
    free (eq->last);
    eq->last = eq->iov[iovcnt - 1].iov_base;

    int i;
    for (i = 0; i < iovcnt - 1; i++) {
        free (eq->iov[i].iov_base);
    }

    memmove (&eq->iov[0],
             &eq->iov[iovcnt],
             sizeof(struct iovec) * (eq->count - iovcnt));
    eq->count -= iovcnt;
}

/**
 * Remove last event sent to communication pipe from internal buffer
 *
 * @param[in] eq A pointer to #event_queue.
 **/
void
event_queue_reset_last (event_queue *eq)
{
    assert (eq != NULL);

    free (eq->last);
    eq->last = NULL;
}
