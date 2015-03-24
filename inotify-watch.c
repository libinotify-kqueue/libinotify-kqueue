/*******************************************************************************
  Copyright (c) 2014 Vladimir Kondratiev <wulf@cicgroup.ru>

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
#include <sys/stat.h>  /* fstat */

#include <assert.h>    /* assert */
#include <errno.h>     /* errno */
#include <fcntl.h>     /* AT_FDCWD */
#include <stdint.h>    /* uint32_t */
#include <stdlib.h>    /* calloc, free */
#include <string.h>    /* strcmp */
#include <unistd.h>    /* close */

#include "sys/inotify.h"

#include "compat.h"
#include "conversions.h"
#include "inotify-watch.h"
#include "utils.h"
#include "watch-set.h"
#include "watch.h"

/**
 * Preform minimal initialization required for opening watch descriptor
 *
 * @param[in] path  Path to watch.
 * @param[in] flags A combination of inotify event flags.
 * @return A inotify watch descriptor on success, -1 otherwise.
 **/
int
iwatch_open (const char *path, uint32_t flags)
{
    if (flags == 0) {
        errno = EINVAL;
        perror_msg ("Failed to open watch %s. Bad event mask %x", path, flags);
        return -1;
    }

    int fd = watch_open (AT_FDCWD, path, flags);
    if (fd == -1) {
        perror_msg ("Failed to open inotify watch %s", path);
    }

    return fd;
}

/**
 * Initialize inotify watch.
 *
 * This function creates and initializes additional watches for a directory.
 *
 * @param[in] wrk    A pointer to #worker.
 * @param[in] fd     A file descriptor of a watched entry.
 * @param[in] flags  A combination of inotify event flags.
 * @return A pointer to a created #i_watch on success NULL otherwise
 **/
i_watch *
iwatch_init (worker *wrk, int fd, uint32_t flags)
{
    assert (wrk != NULL);
    assert (fd != -1);

    struct stat st;
    if (fstat (fd, &st) == -1) {
        perror_msg ("fstat failed on %d", fd);
        return NULL;
    }

    i_watch *iw = calloc (1, sizeof (i_watch));
    if (iw == NULL) {
        perror_msg ("Failed to allocate inotify watch");
        return NULL;
    }

    iw->deps = NULL;
    iw->wrk = wrk;
    iw->wd = fd;
    iw->flags = flags;
    iw->inode = st.st_ino;
    iw->dev = st.st_dev;
    iw->is_closed = 0;

    watch_set_init (&iw->watches);

    if (S_ISDIR (st.st_mode)) {
        iw->deps = dl_listing (fd);
        if (iw->deps == NULL) {
            perror_msg ("Directory listing of %d failed", fd);
            iwatch_free (iw);
            return NULL;
        }
    }

    watch *parent = watch_init (iw, WATCH_USER, fd);
    if (parent == NULL) {
        iwatch_free (iw);
        return NULL;
    }

    watch_set_insert (&iw->watches, parent);

    if (S_ISDIR (st.st_mode)) {

        dep_node *iter;
        SLIST_FOREACH (iter, &iw->deps->head, next) {
            watch *neww = iwatch_add_subwatch (iw, iter->item);
            if (neww == NULL) {
                perror_msg ("Failed to start watching a dependency %s",
                            iter->item->path);
            }
        }
    }
    return iw;
}

/**
 * Free an inotify watch.
 *
 * @param[in] iw      A pointer to #i_watch to remove.
 **/
void
iwatch_free (i_watch *iw)
{
    assert (iw != NULL);

    watch_set_free (&iw->watches);
    if (iw->deps != NULL) {
        dl_free (iw->deps);
    }
    free (iw);
}

/**
 * Start watching a file or a directory.
 *
 * @param[in] iw A pointer to #i_watch.
 * @param[in] di A dependency item with relative path to watch.
 * @return A pointer to a created watch.
 **/
watch*
iwatch_add_subwatch (i_watch *iw, const dep_item *di)
{
    assert (iw != NULL);
    assert (iw->deps != NULL);
    assert (di != NULL);

    if (iw->is_closed) {
        return NULL;
    }

    watch *w = watch_set_find (&iw->watches, di->inode);
    if (w != NULL) {
        goto hold;
    }

    int fd = watch_open (iw->wd, di->path, IN_DONT_FOLLOW);
    if (fd == -1) {
        perror_msg ("Failed to open file %s", di->path);
        return NULL;
    }

    w = watch_init (iw, WATCH_DEPENDENCY, fd);
    if (w == NULL) {
        close (fd);
        return NULL;
    }

    watch_set_insert (&iw->watches, w);

hold:
    ++w->refcount;
    return w;
}

/**
 * Remove a watch from worker by its path.
 *
 * @param[in] iw A pointer to the #i_watch.
 * @param[in] di A dependency list item to remove watch.
 **/
void
iwatch_del_subwatch (i_watch *iw, const dep_item *di)
{
    assert (iw != NULL);
    assert (di != NULL);

    watch *w = watch_set_find (&iw->watches, di->inode);
    if (w != NULL) {
        assert (w->refcount > 0);
        --w->refcount;

        if (w->refcount == 0) {
            watch_set_delete (&iw->watches, w);
        }
    }
}

/**
 * Update inotify watch flags.
 *
 * When called for a directory watch, update also the flags of all the
 * dependent (child) watches.
 *
 * @param[in] iw    A pointer to #i_watch.
 * @param[in] flags A combination of the inotify watch flags.
 **/
void
iwatch_update_flags (i_watch *iw, uint32_t flags)
{
    assert (iw != NULL);

    /* merge flags if IN_MASK_ADD flag is set */
    if (flags & IN_MASK_ADD) {
        flags |= iw->flags;
    }

    iw->flags = flags;

    watch *w;
    RB_FOREACH (w, watch_set, &iw->watches) {
        uint32_t fflags = inotify_to_kqueue (flags,
                                             w->flags & WF_ISDIR,
                                             w->flags & WF_ISSUBWATCH);
        watch_register_event (w, fflags);
    }
}

/**
 * Check if a file under given path is/was a directory. Use worker's
 * cached data (watches) to query file type (this function is called
 * when something happens in a watched directory, so we SHOULD have
 * a watch for its contents
 *
 * @param[in] iw  A inotify watch for which a change has been triggered.
 * @param[in] di  A dependency list item
 *
 * @return 1 if dir (cached), 0 otherwise.
 **/
int
iwatch_subwatch_is_dir (i_watch *iw, const dep_item *di)
{
    watch *w = watch_set_find (&iw->watches, di->inode);
    if (w != NULL && w->flags & WF_ISDIR) {
            return 1;
    }
    return 0;
}
