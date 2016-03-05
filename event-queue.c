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
#include <sys/socket.h>/* SO_NOSIGPIPE */
#include <sys/uio.h>   /* iovec */

#include <assert.h>    /* assert */
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
#ifdef AVOID_SIGPIPE_WITH_SEND
    eq->msgalloc = 0;
    eq->msgbuf = NULL;
#endif
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
#ifdef AVOID_SIGPIPE_WITH_SEND
    free (eq->msgbuf);
#endif
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

#ifdef AVOID_SIGPIPE_WITH_SEND
/**
 * Extend inotify event queue message buffer to iovlen bytes.
 *
 * @param[in] eq     A pointer to #event_queue.
 * @param[in] iovlen New size of buffer
 **/
static int
event_queue_extend_msgbuf (event_queue *eq, size_t iovlen)
{

    if (iovlen > eq->msgalloc) {
        void *ptr = realloc(eq->msgbuf, iovlen);
        if (ptr == NULL) {
            perror_msg ("Failed to extend eq->msgbuf to %d bytes", iovlen);
            return -1;
        }
        eq->msgbuf = ptr;
        eq->msgalloc = iovlen;
    }

    return 0;
}

/**
 * Sendv implementation. Uses #event_queue as temporary buffer.
 *
 * @param[in] eq     A pointer to #event_queue.
 * @param[in] fd     A file descriptor to send to.
 * @param[in] iov    An array of iovec buffers to send.
 * @param[in] iovcnt A number of iovec buffers to send.
 * @param[in] flags  A send(3) flags.
 * @return Number of bytes which were sent on success, -1 on failure.
 **/
static ssize_t
event_queue_sendv(event_queue        *eq,
                  int                 fd,
                  const struct iovec *iov,
                  int                 iovcnt,
                  int                 flags)
{
    int i;
    size_t offset = 0;

    /* Fill msgbuf with iov data */
    for (i = 0; i < iovcnt; i++) {
         memcpy (eq->msgbuf + offset, eq->iov[i].iov_base, eq->iov[i].iov_len);
         offset += eq->iov[i].iov_len;
         assert (offset <= eq->msgalloc);
    }

    return safe_send (fd, eq->msgbuf, offset, flags);
}
#endif

/**
 * Flush inotify events queue to socket
 *
 * @param[in] eq      A pointer to #event_queue.
 * @param[in] fd      A file descriptor to write events
 * @param[in] sbspace Amount of space in socket buffer available to write
 *                    w/o blocking
 **/
void event_queue_flush (event_queue *eq, int fd, size_t sbspace)
{
    int iovcnt, iovmax;
    size_t iovlen = 0;

    iovmax = eq->count;
#ifndef AVOID_SIGPIPE_WITH_SEND
    if (iovmax > IOV_MAX) {
        iovmax = IOV_MAX;
    }
#endif

    for (iovcnt = 0; iovcnt < iovmax; iovcnt++) {
        if (iovlen + eq->iov[iovcnt].iov_len > sbspace) {
            break;
        }
        iovlen += eq->iov[iovcnt].iov_len;
    }

    if (iovcnt == 0) {
        return;
    }

#ifdef AVOID_SIGPIPE_WITH_SEND
    /*
     * Most OSes (Linux, Solaris and FreeBSD) delivers SIGPIPE to thread which
     * issued write (worker thread) as it is syncronous signal. At least some
     * versions of NetBSD, OpenBSD and MacOSX delivers it to any thread in process
     * making blocking SIGPIPE in worker thread useless. As closing of opposite
     * end of the pipe is a legal method of closing inotify we try to prevent
     * SIGPIPE with using a send(3) with MSG_NOSIGNAL flag set and creating
     * the socket pair with SO_NOSIGPIPE option.
     */
    if (event_queue_extend_msgbuf (eq, iovlen) == -1) {
        return;
    }

    int send_flags = 0;
#if defined (MSG_NOSIGNAL)
    send_flags |= MSG_NOSIGNAL;
#endif

    if (event_queue_sendv(eq, fd, eq->iov, iovcnt, send_flags) == -1) {
#else /* ! AVOID_SIGPIPE_WITH_SEND */
    if (safe_writev (fd, eq->iov, iovcnt) == -1) {
#endif
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
