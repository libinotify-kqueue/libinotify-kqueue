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

#include <stddef.h> /* NULL */
#include <assert.h>
#include <stdlib.h> /* calloc, realloc */
#include <string.h> /* memset */
#include <stdio.h>

#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"

#include "utils.h"
#include "conversions.h"
#include "inotify-watch.h"
#include "worker.h"
#include "worker-sets.h"
#include "worker-thread.h"

void worker_erase (worker *wrk);
static void handle_moved (void *udata, dep_item *from_di, dep_item *to_di);

/**
 * Create a new inotify event and place it to event queue.
 *
 * @param[in] iw     A pointer to #i_watch.
 * @param[in] mask   An inotify watch mask.
 * @param[in] cookie Event cookie.
 * @param[in] name   File name (may be NULL).
 * @return 0 on success, -1 otherwise.
 **/
int
enqueue_event (i_watch    *iw,
               uint32_t    mask,
               uint32_t    cookie,
               const char *name)
{
    assert (iw != NULL);
    worker *wrk = iw->wrk;
    assert (wrk != NULL);

    if (wrk->iovcnt >= wrk->iovalloc) {
        int to_allocate = wrk->iovcnt + 1;
        void *ptr = realloc (wrk->iov, sizeof (struct iovec) * to_allocate);
        if (ptr == NULL) {
            perror_msg ("Failed to extend events to %d items", to_allocate);       
            return -1;
        }
        wrk->iov = ptr;
        wrk->iovalloc = to_allocate;
    }

    wrk->iov[wrk->iovcnt].iov_base = create_inotify_event (iw->wd, mask,
        cookie, name, &wrk->iov[wrk->iovcnt].iov_len);

    if (wrk->iov[wrk->iovcnt].iov_base != NULL) {
        ++wrk->iovcnt;
    } else {
        perror_msg ("Failed to create a inotify event %x", mask);
        return -1;
    }

    return 0;
}

/**
 * Flush inotify events queue to socket
 *
 * @param[in] wrk A pointer to #worker.
 **/
void
flush_events (worker *wrk)
{
    if (safe_writev (wrk->io[KQUEUE_FD], wrk->iov, wrk->iovcnt) == -1) {
        perror_msg ("Sending of inotify events to socket failed");
    }

    int i;
    for (i = 0; i < wrk->iovcnt; i++) {
        free (wrk->iov[i].iov_base);
    }

    wrk->iovcnt = 0;
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
static int
check_is_dir_cached (i_watch *iw, const dep_item *di)
{
    int i;
    for (i = 0; i < iw->watches.length; i++) {
        const watch *w = iw->watches.watches[i];
        if (w != NULL && strcmp (di->path, w->filename) == 0 && w->is_really_dir)
            return 1;
    }
    return 0;
}

/**
 * Process a worker command.
 *
 * @param[in] wrk A pointer to #worker.
 **/
void
process_command (worker *wrk)
{
    assert (wrk != NULL);

    /* read a byte */
    char unused;
    safe_read (wrk->io[KQUEUE_FD], &unused, 1);

    if (wrk->cmd.type == WCMD_ADD) {
        wrk->cmd.retval = worker_add_or_modify (wrk,
                                                wrk->cmd.add.filename,
                                                wrk->cmd.add.mask);
    } else if (wrk->cmd.type == WCMD_REMOVE) {
        wrk->cmd.retval = worker_remove (wrk, wrk->cmd.rm_id);
    } else {
        perror_msg ("Worker processing a command without a command - "
                    "something went wrong.");
        return;
    }

    /* TODO: is the situation when nobody else waits on a barrier possible? */
    worker_cmd_wait (&wrk->cmd);
}

/** 
 * This structure represents a directory diff calculation context.
 * It is passed to dl_calculate as user data and then is used in all
 * the callbacks.
 **/
typedef struct {
    i_watch *iw;
} handle_context;

/**
 * Produce an IN_CREATE notification for a new file and start wathing on it.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] di     File name & inode number of a new file.
 **/
static void
handle_added (void *udata, dep_item *di)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->iw != NULL);

    int addMask = 0;
    watch *neww = worker_add_subwatch (ctx->iw, di);
        if (neww == NULL) {
            perror_msg ("Failed to start watching on a new dependency %s", di->path);
        } else {
            if (neww->is_really_dir) {
                addMask = IN_ISDIR;
            }
        }

    enqueue_event (ctx->iw, IN_CREATE | addMask, 0, di->path);
}

/**
 * Produce an IN_DELETE notification for a removed file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] di     File name & inode number of the removed file.
 **/
static void
handle_removed (void *udata, dep_item *di)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->iw != NULL);

    int addMask = check_is_dir_cached (ctx->iw, di) ? IN_ISDIR : 0;
    enqueue_event (ctx->iw, IN_DELETE | addMask, 0, di->path);
    worker_remove_watch (ctx->iw, di);
}

/**
 * Stop watching on the replaced file.
 * Do not produce an IN_MOVED_FROM/IN_MOVED_TO notifications pair
 * for a replaced file as it has already been done on handle_moved call.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata A pointer to user data (#handle_context).
 * @param[in] di    A file name & inode number of the replaced file.
 **/
static void
handle_replaced (void *udata, dep_item *di)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->iw != NULL);

    worker_remove_watch (ctx->iw, di);
}

/**
 * Produce an IN_DELETE/IN_CREATE notifications pair for an overwritten file.
 * Reopen a watch for the overwritten file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata   A pointer to user data (#handle_context).
 * @param[in] from_di A file name & inode number of the deleted file.
 * @param[in] to_di   A file name & inode number of the appeared file.
 **/
static void
handle_overwritten (void *udata, dep_item *from_di, dep_item *to_di)
{
    assert (udata != NULL);

    handle_removed (udata, from_di);
    handle_added (udata, to_di);
}

/**
 * Produce an IN_MOVED_FROM/IN_MOVED_TO notifications pair for a renamed file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata   A pointer to user data (#handle_context).
 * @param[in] from_di A old name & inode number of the file.
 * @param[in] to_di   A new name & inode number of the file.
 **/
static void
handle_moved (void *udata, dep_item *from_di, dep_item *to_di)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->iw != NULL);

    int addMask = check_is_dir_cached (ctx->iw, from_di) ? IN_ISDIR : 0;
    uint32_t cookie = from_di->inode & 0x00000000FFFFFFFF;

    enqueue_event (ctx->iw, IN_MOVED_FROM | addMask, cookie, from_di->path);
    enqueue_event (ctx->iw, IN_MOVED_TO | addMask, cookie, to_di->path);
    worker_rename_watch (ctx->iw, from_di, to_di);
}


static const traverse_cbs cbs = {
    NULL, /* handle_unchanged */
    handle_added,
    handle_removed,
    handle_replaced,
    handle_overwritten,
    handle_moved,
    NULL, /* many_added */
    NULL, /* many_removed */
    NULL, /* names_updated */
};

/**
 * Detect and notify about the changes in the watched directory.
 *
 * This function is top-level and it operates with other specific routines
 * to notify about different sets of events in a different conditions.
 *
 * @param[in] iw    A pointer to #i_watch.
 * @param[in] event A pointer to the received kqueue event.
 **/
void
produce_directory_diff (i_watch *iw, struct kevent *event)
{
    assert (iw != NULL);
    assert (event != NULL);

    dep_list *was = NULL, *now = NULL;
    was = iw->deps;
    now = dl_listing (iw->wd);
    if (now == NULL) {
        perror_msg ("Failed to create a listing for watch %d", iw->wd);
        return;
    }

    iw->deps = now;

    handle_context ctx;
    memset (&ctx, 0, sizeof (ctx));
    ctx.iw = iw;
    
    if (dl_calculate (was, now, &cbs, &ctx) == -1) {
        iw->deps = was;
        dl_free (now);
        perror_msg ("Failed to produce directory diff for watch %d", iw->wd);
    }
}

/**
 * Produce notifications about file system activity observer by a worker.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] event A pointer to the associated received kqueue event.
 **/
void
produce_notifications (worker *wrk, struct kevent *event)
{
    assert (wrk != NULL);
    assert (event != NULL);

    watch *w = NULL;
    i_watch *iw = NULL;
    size_t i;

    SLIST_FOREACH (iw, &wrk->head, next) {
        for (i = 0; i < iw->watches.length; i++) {
            if (event->ident == iw->watches.watches[i]->fd) {
                w = iw->watches.watches[i];
                goto found;
            }
        }
    }

found:
    assert (iw != NULL);
    assert (w != NULL);

    uint32_t flags = event->fflags;

    if (w->type == WATCH_USER) {
        /* Treat deletes as link number changes if links still exist */
        if (flags & NOTE_DELETE && !w->is_really_dir && !is_deleted (w->fd)) {
            flags = (flags | NOTE_LINK) & ~NOTE_DELETE;
        }

        if (flags & NOTE_WRITE && w->is_directory) {
            produce_directory_diff (iw, event);
            flags &= ~(NOTE_WRITE | NOTE_EXTEND | NOTE_LINK);
        }

        if (flags) {
            enqueue_event (iw,
                           kqueue_to_inotify (flags, w->is_really_dir, 0),
                           0,
                           NULL);
        }

        if (flags & NOTE_DELETE) {
            worker_remove (wrk, iw->wd);
        }
    } else {
        /* for dependency events, ignore some notifications */
        if (flags & (NOTE_ATTRIB | NOTE_LINK | NOTE_WRITE)) {
            enqueue_event (iw,
                           kqueue_to_inotify (flags, w->is_really_dir, 1),
                           0,
                           w->filename);
        }
    }
    flush_events (wrk);
}

/**
 * The worker thread command loop.
 *
 * @param[in] arg A pointer to the associated #worker.
 * @return NULL. 
**/
void*
worker_thread (void *arg)
{
    assert (arg != NULL);
    worker* wrk = (worker *) arg;

    for (;;) {
        struct kevent received;

        int ret = kevent (wrk->kq, NULL, 0, &received, 1, NULL);
        if (ret == -1) {
            perror_msg ("kevent failed");
            continue;
        }

        if (received.ident == wrk->io[KQUEUE_FD]) {
            if (received.flags & EV_EOF) {
                wrk->closed = 1;
                wrk->io[INOTIFY_FD] = -1;
                worker_erase (wrk);

                if (pthread_mutex_trylock (&wrk->mutex) == 0) {
                    pthread_mutex_unlock (&wrk->mutex);
                    worker_free (wrk);
                }
                /* If we could not lock on a worker, it means that an inotify
                 * call (add_watch/rm_watch) has already locked it. In this
                 * case worker will be freed by a caller (caller checks the
                 * `closed' flag. */
                return NULL;
            } else {
                process_command (wrk);
            }
        } else {
            produce_notifications (wrk, &received);
        }
    }
    return NULL;
}
