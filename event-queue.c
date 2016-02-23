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

#include <sys/types.h> /* uint32_t */
#include <sys/uio.h>   /* iovec */

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
    int retval = 0;

    if (eq->count > MAX_QUEUED_EVENTS) {
        return -1;
    }

    if (event_queue_extend (eq) == -1) {
        return -1;
    }

    if (eq->count == MAX_QUEUED_EVENTS) {
        wd = -1;
        mask = IN_Q_OVERFLOW;
        cookie = 0;
        name = NULL;
        retval = -1;
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
 * @param[in] fd      A pair of descriptors used in referencing the pipe
 * @param[in] sbspace Amount of space in socket buffer available to write
 *                    w/o blocking
 **/
void event_queue_flush (event_queue *eq, int fd[2], size_t sbspace)
{
    int iovcnt;
    size_t iovlen = 0;

    for (iovcnt = 0; iovcnt < eq->count; iovcnt++) {
        iovlen += eq->iov[iovcnt].iov_len;
        if (iovlen > sbspace) {
            break;
        }
    }

#ifdef SIGPIPE_RECIPIENT_IS_PROCESS
    /*
     * Most OSes (Linux, Solaris and FreeBSD) delivers SIGPIPE to thread which
     * issued write (worker thread) as it is syncronous signal. At least some
     * versions of NetBSD and OpenBSD delivers it to any thread in process
     * making blocking SIGPIPE in worker thread useless. As closing of opposite
     * end of the pipe is a legal method of closing inotify we try to reduce
     * chances of SIGPIPE in this case with extra check.
     */
    if (is_opened (fd[INOTIFY_FD]))
#endif
    if (safe_writev (fd[KQUEUE_FD], eq->iov, iovcnt) == -1) {
        perror_msg ("Sending of inotify events to socket failed");
    }

    int i;
    for (i = 0; i < iovcnt; i++) {
        free (eq->iov[i].iov_base);
    }

    memmove (&eq->iov[0],
             &eq->iov[iovcnt],
             sizeof(struct iovec) * (eq->count - iovcnt));
    eq->count -= iovcnt;
}
