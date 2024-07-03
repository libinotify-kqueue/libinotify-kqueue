/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <sys/types.h>
#include <sys/event.h> /* kqueue[1] */
#include <sys/socket.h>/* send, sendmsg */
#include <sys/stat.h>  /* fstat */
#include <sys/uio.h>   /* writev */

#include <assert.h>
#include <errno.h>  /* EINTR */
#include <fcntl.h>  /* fcntl */
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen */
#include <unistd.h> /* read, write */

#include "sys/inotify.h"

#include "compat.h"
#include "config.h"
#include "utils.h"

static const struct timespec zero_ts = { 0, 0 };
const struct timespec *zero_tsp = &zero_ts;

#ifdef ENABLE_PERRORS
#include <stdarg.h>
#include <pthread.h>
pthread_mutex_t perror_msg_mutex = PTHREAD_MUTEX_INITIALIZER;
char *
perror_msg_printf (const char *fmt, ...)
{
    static char buf[200];
    va_list ap;

    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    return buf;
}
#endif

int
kqueue_init (void)
{
    int kq;
#ifdef HAVE_KQUEUE1
    kq = kqueue1 (O_CLOEXEC);
#else
    kq = kqueue ();
    if (kq != -1) {
        (void)set_cloexec_flag (kq, 1);
    }
#endif
    if (kq == -1) {
        perror_msg (("Failed to create a new kqueue"));
    }
    return kq;
}

/**
 * Create a new inotify event.
 *
 * @param[in] wd     An associated watch's id.
 * @param[in] mask   An inotify watch mask.
 * @param[in] cookie Event cookie.
 * @param[in] name   File name (may be NULL).
 * @param[out] event_len The length of the created event, in bytes.
 * @return A pointer to a created event on NULL on a failure.
 **/
struct inotify_event*
create_inotify_event (int         wd,
                      uint32_t    mask,
                      uint32_t    cookie,
                      const char *name,
                      size_t     *event_len)
{
    struct inotify_event *event = NULL;
    size_t name_len = name ? strlen (name) + 1 : 0;
    *event_len = offsetof (struct inotify_event, name) + name_len;
    event = calloc (1, *event_len);

    if (event == NULL) {
        perror_msg (("Failed to allocate a new inotify event [%s, %X]",
                    name,
                    mask));
        return NULL;
    }

    event->wd = wd;
    event->mask = mask;
    event->cookie = cookie;
    event->len = name_len;

    if (name) {
        strlcpy (event->name, name, name_len);
    }

    return event;
}


/**
 * scatter-gather version of send with writev()-style parameters.
 *
 * @param[in] fd     A file descriptor to send to.
 * @param[in] iov    An array of iovec buffers to wtite.
 * @param[in] iovcnt A number of iovec buffers to write.
 * @param[in] flags  A send(3) flags.
 * @return Number of bytes which were written on success, -1 on failure.
 **/
ssize_t
sendv (int fd, struct iovec iov[], int iovcnt, int flags)
{
    struct msghdr msg;

    memset (&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iovcnt;

    return (sendmsg (fd, &msg, flags));
}

/**
 * Check if the specified file descriptor is still opened.
 *
 * @param[in] fd A file descriptor to check.
 * @return 1 if still opened, 0 if closed or an error has occured.
 **/
int
is_opened (int fd)
{
    int ret = (fcntl (fd, F_GETFL) != -1);
    return ret;
}

/**
 * Check if the file referenced by specified descriptor is deleted.
 *
 * @param[in] fd A file descriptor to check.
 * @return 1 if deleted or error occured, 0 if hardlinks to file still exist.
 **/
int
is_deleted (int fd)
{
    struct stat st;

    if (fstat (fd, &st) == -1) {
        if (errno != ENOENT) {
            perror_msg (("fstat %d failed", fd));
        }
        return 1;
    }

    return (st.st_nlink == 0);
}

/**
 * Set the FD_CLOEXEC flag of file descriptor fd if value is nonzero
 * clear the flag if value is 0.
 *
 * @param[in] fd    A file descriptor to modify.
 * @param[in] value A cloexec flag value to set.
 * @return 0 on success, or -1 on error with errno set.
 **/
int
set_cloexec_flag (int fd, int value)
{
    int flags = fcntl (fd, F_GETFD, 0);
    if (flags < 0)
        return flags;

    if (value != 0)
        flags |= FD_CLOEXEC;
    else
        flags &= ~FD_CLOEXEC;

    return fcntl (fd, F_SETFD, flags);
}

/*
 * Set the O_NONBLOCK flag of file descriptor fd if value is nonzero
 * clear the flag if value is 0.
 *
 * @param[in] fd    A file descriptor to modify.
 * @param[in] value A nonblock flag value to set.
 * @return 0 on success, or -1 on error with errno set.
 **/
int
set_nonblock_flag (int fd, int value)
{
    int flags = fcntl (fd, F_GETFL, 0);
    if (flags < 0)
        return flags;

    if (value != 0)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return fcntl (fd, F_SETFL, flags);
}

/**
 * Set size of socket buffer
 *
 * @param[in] fd  A file descriptor (send side).
 * @param[in] len A new size of socket buffer.
 * @return 0 on success, or -1 on error with errno set.
 **/
int
set_sndbuf_size (int fd, int len)
{
    return setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &len, sizeof(len));
}

/**
 * Perform dup(2) and set the FD_CLOEXEC flag on the new file descriptor
 *
 * @param[in] oldd A file descriptor to duplicate.
 * @return A new file descriptor on success, or -1 if an error occurs.
 *      The external variable errno indicates the cause of the error.
 **/
int
dup_cloexec (int oldd)
{
#ifdef F_DUPFD_CLOEXEC
    int newd = fcntl (oldd, F_DUPFD_CLOEXEC, 0);
#else
    int newd = fcntl (oldd, F_DUPFD, 0);

    if ((newd != -1) && (set_cloexec_flag (newd, 1) == -1)) {
        close (newd);
        newd = -1;
    }
#endif
    return newd;
}

/**
 * Open directory one more time by realtive path "."
 *
 * @param[in] fd A file descriptor to inherit
 * @return A new file descriptor on success, or -1 if an error occured.
 **/
DIR *
fdreopendir (int oldd)
{
    DIR *dir;

#if (READDIR_DOES_OPENDIR == 2) && ! defined (HAVE_FDOPENDIR)
    char *dirpath = fd_getpath_cached (oldd);
    if (dirpath == NULL) {
        return NULL;
    }
    dir = opendir (dirpath);
#else /* READDIR_DOES_OPENDIR == 2 && ! HAVE_FDOPENDIR */
    int fd;
#if (READDIR_DOES_OPENDIR == 2)
    int openflags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
        openflags |= O_CLOEXEC;
#endif
#ifdef HAVE_O_EMPTY_PATH
    fd = openat (oldd, "", openflags | O_EMPTY_PATH);
#else
    fd = openat (oldd, ".", openflags);
#endif
#elif (READDIR_DOES_OPENDIR == 1)
    fd = dup_cloexec (oldd);
#elif (READDIR_DOES_OPENDIR == 0)
    fd = oldd;
#else
#error unknown READDIR_DOES_OPENDIR value
#endif
    if (fd == -1) {
        return NULL;
    }

#if (READDIR_DOES_OPENDIR < 2)
    /*
     * Rewind directory content as fdopendir() does not do it for us.
     * Note: rewinddir() right after fdopendir() is not working here
     * due to rewinddir() bug in some versions of FreeBSD and Darwin libc.
     */
    lseek (fd, 0, SEEK_SET);
#endif

#if (READDIR_DOES_OPENDIR == 2) && ! defined(O_CLOEXEC)
    set_cloexec_flag (fd, 1);
#endif

    dir = fdopendir (fd);
#if (READDIR_DOES_OPENDIR > 0)
    if (dir == NULL) {
        close (fd);
    }
#endif
#endif /* READDIR_DOES_OPENDIR != 2 && HAVE_FDOPENDIR */

    return dir;
}
