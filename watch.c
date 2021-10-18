/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#include "compat.h"

#include <errno.h>  /* errno */
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
#include "watch.h"
#include "sys/inotify.h"

/**
 * Convert the inotify watch mask to the kqueue event filter flags.
 *
 * @param[in] flags An inotify watch mask.
 * @param[in] wf    A kqueue watch internal flags.
 * @return Converted kqueue event filter flags.
 **/
uint32_t
inotify_to_kqueue (uint32_t flags, mode_t mode, bool is_parent)
{
    uint32_t result = 0;

    if (!(S_ISREG (mode) || S_ISDIR (mode) || S_ISLNK (mode))) {
        return result;
    }

#ifdef NOTE_OPEN
    if (flags & IN_OPEN)
        result |= NOTE_OPEN;
#endif
#ifdef NOTE_CLOSE
    if (flags & IN_CLOSE_NOWRITE)
        result |= NOTE_CLOSE;
#endif
#ifdef NOTE_CLOSE_WRITE
    if (flags & IN_CLOSE_WRITE && S_ISREG (mode))
        result |= NOTE_CLOSE_WRITE;
#endif
#ifdef NOTE_READ
    if (flags & IN_ACCESS && (S_ISREG (mode) || S_ISDIR (mode)))
        result |= NOTE_READ;
#endif
    if (flags & IN_ATTRIB)
        result |= NOTE_ATTRIB;
    if (flags & IN_MODIFY && S_ISREG (mode))
        result |= NOTE_WRITE;
    if (is_parent) {
        if (S_ISDIR (mode)) {
            result |= NOTE_WRITE;
#if defined(HAVE_NOTE_EXTEND_ON_MOVE_TO) || \
    defined(HAVE_NOTE_EXTEND_ON_MOVE_FROM)
            result |= NOTE_EXTEND;
#endif
        }
        if (flags & IN_ATTRIB && S_ISREG (mode))
            result |= NOTE_LINK;
        if (flags & IN_MOVE_SELF)
            result |= NOTE_RENAME;
        result |= NOTE_DELETE | NOTE_REVOKE;
    }
    return result;
}

/**
 * Convert the kqueue event filter flags to the inotify watch mask.
 *
 * @param[in] flags A kqueue filter flags.
 * @param[in] wf    A kqueue watch internal flags.
 * @return Converted inotify watch mask.
 **/
uint32_t
kqueue_to_inotify (uint32_t flags,
                   mode_t mode,
                   bool is_parent,
                   bool is_deleted)
{
    uint32_t result = 0;

#ifdef NOTE_OPEN
    if (flags & NOTE_OPEN)
        result |= IN_OPEN;
#endif
#ifdef NOTE_CLOSE
    if (flags & NOTE_CLOSE)
        result |= IN_CLOSE_NOWRITE;
#endif
#ifdef NOTE_CLOSE_WRITE
    if (flags & NOTE_CLOSE_WRITE)
        result |= IN_CLOSE_WRITE;
#endif
#ifdef NOTE_READ
    if (flags & NOTE_READ && (S_ISREG (mode) || S_ISDIR (mode)))
        result |= IN_ACCESS;
#endif

    if (flags & NOTE_ATTRIB ||                /* attribute changes */
        (flags & (NOTE_LINK | NOTE_DELETE) && /* link number changes */
         S_ISREG (mode) && is_parent))
        result |= IN_ATTRIB;

    if (flags & NOTE_WRITE && S_ISREG (mode))
        result |= IN_MODIFY;

    /* Do not issue IN_DELETE_SELF if links still exist */
    if (flags & NOTE_DELETE && is_parent && (is_deleted || !S_ISREG (mode)))
        result |= IN_DELETE_SELF;

    if (flags & NOTE_RENAME && is_parent)
        result |= IN_MOVE_SELF;

    if (flags & NOTE_REVOKE && is_parent)
        result |= IN_UNMOUNT;

    /* IN_ISDIR flag for subwatches is set in the enqueue_event routine */
    if ((result & (IN_ATTRIB | IN_OPEN | IN_ACCESS | IN_CLOSE))
        && S_ISDIR (mode) && is_parent) {
        result |= IN_ISDIR;
    }

    return result;
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
 * @param[in] kq     A kqueue descriptor
 * @param[in] fflags A filter flags in kqueue format
 * @return 1 on success, -1 on error and 0 if no events have been registered
 **/
int
watch_register_event (watch *w, int kq, uint32_t fflags)
{
    assert (w != NULL);
    assert (kq != -1);

    struct kevent ev;
    int result;

    if (fflags == w->fflags) {
        return 0;
    }

    EV_SET (&ev,
            w->fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_CLEAR,
            fflags,
            0,
            PTR_TO_UDATA (w));

    result = kevent (kq, &ev, 1, NULL, 0, NULL);

    if (result != -1) {
        w->fflags = fflags;
    }

    return result;
}

/**
 * Calculates kqueue filter flags for a #watch with traversing depedencies.
 * and register vnode kqueue watch in kernel kqueue(2) subsystem
 *
 * @param[in] w  A pointer to the #watch.
 * @return 1 on success, -1 on error and 0 if no events have been registered
 **/
int
watch_update_event (watch *w)
{
    assert (w != NULL);
    assert (!watch_deps_empty (w));

    int kq = SLIST_FIRST(&w->deps)->iw->wrk->kq;
    uint32_t fflags = 0;
    struct watch_dep *wd;

    WD_FOREACH (wd, w) {
        fflags |= inotify_to_kqueue (wd->iw->flags,
                                     watch_dep_get_mode (wd),
                                     watch_dep_is_parent (wd));
    }
    assert (fflags != 0);

    return (watch_register_event (w, kq, fflags));
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

    int openflags = O_NONBLOCK;
#ifdef O_EVTONLY
    openflags |= O_EVTONLY;
#else
    openflags |= O_RDONLY;
#endif
#ifdef O_CLOEXEC
    openflags |= O_CLOEXEC;
#endif
    if (flags & IN_DONT_FOLLOW) {
#ifdef O_SYMLINK
        openflags |= O_SYMLINK;
#else
        openflags |= O_NOFOLLOW;
#endif
    }
#ifdef O_DIRECTORY
    if (flags & IN_ONLYDIR) {
        openflags |= O_DIRECTORY;
    }
#endif

    int fd = openat (dirfd, path, openflags);
    if (fd == -1) {
        return -1;
    }

#ifndef O_DIRECTORY
    if (flags & IN_ONLYDIR) {
        struct stat st;
        if (fstat (fd, &st) == -1) {
            perror_msg ("Failed to fstat on watch open %s", path);
            close (fd);
            return -1;
        }

        if (!S_ISDIR (st.st_mode)) {
            errno = ENOTDIR;
            close (fd);
            return -1;
        }
    }
#endif

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
 * @param[in] fd A file descriptor of a watched entry.
 * @param[in] st A stat structure of watch.
 * @return A pointer to a watch on success, NULL on failure.
 **/
watch *
watch_init (int fd, struct stat *st)
{
    assert (fd != -1);

    watch *w = calloc (1, sizeof (struct watch));
    if (w == NULL) {
        perror_msg ("Failed to allocate watch");
        return NULL;
    }

    w->fd = fd;
    w->fflags = 0;
    w->skip_next = false;
    SLIST_INIT (&w->deps);
    w->dev = st->st_dev;
    /* Inode number obtained via fstat call cannot be used here as it
     * differs from readdir`s one at mount points. */
    w->inode = st->st_ino;

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
    while (!watch_deps_empty (w)) {
        struct watch_dep *wd = SLIST_FIRST (&w->deps);
        SLIST_REMOVE_HEAD (&w->deps, next);
        free (wd);
    }
    free (w);
}

/**
 * Calculates #watch file status with traversing depedencies.
 *
 * @param[in] w  A pointer to the #watch.
 * @return mode in stat() format.
 **/
mode_t
watch_get_mode (watch *w)
{
    assert (w != NULL);
    assert (!watch_deps_empty (w));

    mode_t mode = watch_dep_get_mode (SLIST_FIRST(&w->deps));
    assert (!S_ISUNK (mode));

    return (mode);
}


/**
 * Find a file dependency associated with a #watch.
 *
 * @param[in] w  A pointer to the #watch.
 * @param[in] iw A pointer to a parent #i_watch.
 * @param[in] di A pointer to name & inode number of the file.
 * @return A pointer to a dependency record if found. NULL otherwise.
 **/
struct watch_dep *
watch_find_dep (watch *w, i_watch *iw, const dep_item *di)
{
    assert (w != NULL);
    assert (iw != NULL);

    struct watch_dep *wd;

    WD_FOREACH (wd, w) {
        if (wd->iw == iw && wd->di == di) {
            return (wd);
        }
    }

    return (NULL);
}

/**
 * Associate a file dependency with a #watch.
 *
 * @param[in] w  A pointer to the #watch.
 * @param[in] iw A pointer to a parent #i_watch.
 * @param[in] di A name & inode number of the associated file.
 * @return A pointer to a created dependency record. NULL on failure.
 **/
struct watch_dep *
watch_add_dep (watch *w, i_watch *iw, const dep_item *di)
{
    assert (w != NULL);
    assert (iw != NULL);

    struct watch_dep *wd = calloc (1, sizeof (struct watch_dep));
    if (wd != NULL) {
        wd->iw = iw;
        wd->di = di;

        uint32_t fflags = inotify_to_kqueue (iw->flags,
                                             watch_dep_get_mode (wd),
                                             watch_dep_is_parent (wd));
        /* It's too late to skip watches with empty kqueue filter flags here */
        assert (fflags != 0);

        fflags |= w->fflags;
        if (watch_register_event (w, iw->wrk->kq, fflags) == -1) {
            free (wd);
            return NULL;
        }

        SLIST_INSERT_HEAD (&w->deps, wd, next);
    }
    return (wd);
}

/**
 * Disassociate file dependency from a #watch.
 *
 * @param[in] w  A pointer to the #watch.
 * @param[in] iw A pointer to a parent #i_watch.
 * @param[in] di A name & inode number of the disassociated file.
 * @return A pointer to a diassociated dependency record. NULL if not found.
 **/
struct watch_dep *
watch_del_dep (watch *w, i_watch *iw, const dep_item *di)
{
    assert (w != NULL);
    assert (iw != NULL);

    struct watch_dep *wd = watch_find_dep (w, iw, di);
    if (wd != NULL) {
        SLIST_REMOVE (&w->deps, wd, watch_dep, next);
        free (wd);
        if (watch_deps_empty (w)) {
            watch_set_delete (&iw->watches, w);
        } else {
            watch_update_event (w);
        }
    }
    return (wd);
}

/**
 * Update a file dependency associated with a #watch.
 *
 * @param[in] w       A pointer to the #watch.
 * @param[in] iw      A pointer to a parent #i_watch.
 * @param[in] di_from A old name & inode number of the file.
 * @param[in] di_to   A new name & inode number of the file.
 * @return A pointer to a updated dependency record. NULL if not found.
 **/
struct watch_dep *
watch_chg_dep (watch *w,
               i_watch *iw,
               const dep_item *di_from,
               const dep_item *di_to)
{
    assert (w != NULL);
    assert (iw != NULL);
    assert (di_from != NULL);
    assert (di_to != NULL);
    assert (di_from->inode == di_to->inode);

    struct watch_dep *wd = watch_find_dep (w, iw, di_from);
    if (wd != NULL) {
        wd->di = di_to;
    }
    return (wd);
}
