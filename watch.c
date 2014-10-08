/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>

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

#include <fcntl.h>  /* open */
#include <unistd.h> /* close */
#include <string.h> /* strdup */
#include <stdlib.h> /* free */
#include <assert.h>

#include <sys/types.h>
#include <sys/event.h> /* kevent */
#include <sys/stat.h> /* stat */
#include <stdio.h>    /* snprintf */

#include "utils.h"
#include "compat.h"
#include "conversions.h"
#include "watch.h"
#include "sys/inotify.h"

/**
 * Get some file information by its file descriptor.
 *
 * @param[in]  fd      A file descriptor.
 * @param[out] is_dir  A flag indicating directory.
 * @param[out] inode   A file's inode number.
 **/
static void
_file_information (int fd, int *is_dir, ino_t *inode)
{
    assert (fd != -1);
    assert (is_dir != NULL);
    assert (inode != NULL);

    struct stat st;
    memset (&st, 0, sizeof (struct stat));

    if (fstat (fd, &st) == -1) {
        perror_msg ("fstat failed on %d, assuming it is just a file", fd);
        return;
    }

    if (is_dir != NULL) {
        *is_dir = S_ISDIR (st.st_mode);
    }

    if (inode != NULL) {
        *inode = st.st_ino;
    }
}

/* struct kevent is declared slightly differently on the different BSDs.
 * This macros will help to avoid cast warnings on the supported platforms. */
#if defined (__NetBSD__)
#define PTR_TO_UDATA(X) ((intptr_t)X)
#else
#define PTR_TO_UDATA(X) (X)
#endif

/**
 * Register vnode kqueue watch in kernel kqueue(2) subsystem
 *
 * @param[in] w      A pointer to a watch
 * @param[in] fflags A filter flags in kqueue format
 * @return 1 on success, -1 on error and 0 if no events have been registered
 **/
int
watch_register_event (watch *w, uint32_t fflags)
{
    assert (w != NULL);
    int kq = w->iw->wrk->kq;
    assert (kq != -1);

    struct kevent ev;

    EV_SET (&ev,
            w->fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_CLEAR,
            fflags,
            0,
            PTR_TO_UDATA (w));

    return kevent (kq, &ev, 1, NULL, 0, NULL);
}

/**
 * Opens a file descriptor of kqueue watch
 *
 * @param[in] dirfd A filedes of parent directory or AT_FDCWD.
 * @param[in] path  A pointer to filename
 * @param[in] flags A watch flags in inotify format
 * @return A file descriptor of opened kqueue watch
 **/
int
watch_open (int dirfd, const char *path, uint32_t flags)
{
    assert (path != NULL);

    int openflags = O_EVTONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
        openflags |= O_CLOEXEC;
#endif
    if (flags & IN_DONT_FOLLOW) {
        openflags |= O_SYMLINK;
    }

    int fd = openat (dirfd, path, openflags);
    if (fd == -1) {
        return -1;
    }

#ifndef O_CLOEXEC
    if (set_cloexec_flag (fd, 1) == -1) {
        close (fd);
        return -1;
    }
#endif

    return fd;
}

/**
 * Initialize a watch.
 *
 * @param[in] iw;        A backreference to parent #i_watch.
 * @param[in] watch_type The type of the watch.
 * @param[in] fd         A file descriptor of a watched entry.
 * @return A pointer to a watch on success, NULL on failure.
 **/
watch *
watch_init (i_watch *iw, watch_type_t watch_type, int fd)
{
    assert (iw != NULL);
    assert (fd != -1);

    watch *w = calloc (1, sizeof (struct watch));
    if (w == NULL) {
        perror_msg ("Failed to allocate watch");
        return NULL;
    }

    w->iw = iw;
    w->fd = fd;
    w->flags = watch_type != WATCH_USER ? WF_ISSUBWATCH : 0;
    w->refcount = 0;

    int is_dir = 0;
    _file_information (w->fd, &is_dir, &w->inode);
    w->flags |= is_dir ? WF_ISDIR : 0;

    uint32_t fflags = inotify_to_kqueue (iw->flags,
                                         w->flags & WF_ISDIR,
                                         w->flags & WF_ISSUBWATCH);
    if (watch_register_event (w, fflags) == -1) {
        free (w);
        return NULL;
    }

    return w;
}

/**
 * Free a watch and all the associated memory.
 *
 * @param[in] w A pointer to a watch.
 **/
void
watch_free (watch *w)
{
    assert (w != NULL);
    if (w->fd != -1) {
        close (w->fd);
    }
    free (w);
}
