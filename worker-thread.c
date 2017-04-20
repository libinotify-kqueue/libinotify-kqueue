/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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

#include <stddef.h> /* NULL */
#include <assert.h>
#include <errno.h>  /* errno */
#include <stdlib.h> /* calloc, realloc */
#include <string.h> /* memset */
#include <stdio.h>

#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"

#include "config.h"
#include "utils.h"
#include "inotify-watch.h"
#include "watch.h"
#include "worker.h"
#include "worker-thread.h"

void worker_erase (worker *wrk);
static void handle_moved (void *udata, dep_item *from_di, dep_item *to_di);

/**
 * Create a new inotify event and place it to event queue.
 *
 * @param[in] iw   A pointer to #i_watch.
 * @param[in] mask An inotify watch mask.
 * @param[in] di   A pointer to dependency item for subfiles (NULL for user).
 * @return 0 on success, -1 otherwise.
 **/
static int
enqueue_event (i_watch *iw, uint32_t mask, const dep_item *di)
{
    assert (iw != NULL);
    worker *wrk = iw->wrk;
    assert (wrk != NULL);

    /*
     * Only IN_ALL_EVENTS, IN_UNMOUNT and IN_ISDIR events are allowed to be
     * reported here. IN_Q_OVERFLOW and IN_IGNORED are directly inserted into
     * event queue from other pieces of code
     */
    mask &= (IN_ALL_EVENTS & iw->flags) | IN_UNMOUNT | IN_ISDIR;
    /* Skip empty IN_ISDIR events and events from closed watches */
    if (!(mask & (IN_ALL_EVENTS | IN_UNMOUNT)) || iw->is_closed) {
        return 0;
    }

    if (iw->flags & IN_ONESHOT) {
        iw->is_closed = 1;
    }

    const char *name = NULL;
    uint32_t cookie = 0;
    if (di != NULL) {
        name = di->path;
        if (mask & IN_MOVE) {
            cookie = di->inode & 0x00000000FFFFFFFF;
        }
        if (S_ISDIR (di->type)) {
            mask |= IN_ISDIR;
        }
    }

    if (event_queue_enqueue (&wrk->eq, iw->wd, mask, cookie, name) == -1) {
        perror_msg ("Failed to enqueue a inotify event %x", mask);
        return -1;
    }

    return 0;
}

/**
 * Process a worker command.
 *
 * @param[in] wrk A pointer to #worker.
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
process_command (worker *wrk, worker_cmd *cmd)
{
    assert (wrk != NULL);

    switch (cmd->type) {
    case WCMD_ADD:
        cmd->retval = worker_add_or_modify (wrk,
                                            cmd->add.filename,
                                            cmd->add.mask);
        cmd->error = errno;
        break;
    case WCMD_REMOVE:
        cmd->retval = worker_remove (wrk, cmd->rm_id);
        cmd->error = errno;
    case WCMD_PARAM:
        cmd->retval = worker_set_param (wrk,
                                        cmd->param.param,
                                        cmd->param.value);
        cmd->error = errno;
    default:
        perror_msg ("Worker processing a command without a command - "
                    "something went wrong.");
        cmd->retval = -1;
        cmd->error = EINVAL;
    }

    worker_post (wrk);
}

/** 
 * This structure represents a directory diff calculation context.
 * It is passed to dl_calculate as user data and then is used in all
 * the callbacks.
 **/
typedef struct {
    i_watch *iw;
    uint32_t fflags;
} handle_context;

/**
 * Copy type of item from old directory listing to a new one.
 * Do not produces any notifications.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata   A pointer to user data (#handle_context).
 * @param[in] from_di The old name & inode number of the file.
 * @param[in] to_di   The new name & inode number of the file.
 **/
static void
handle_unchanged (void *udata, dep_item *from_di, dep_item *to_di)
{
    assert (udata != NULL);

    if (to_di->type == S_IFUNK) {
        to_di->type = from_di->type;
    }
}

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

    iwatch_add_subwatch (ctx->iw, di);
#ifdef HAVE_NOTE_EXTEND_ON_SUBFILE_RENAME
    if (ctx->fflags & NOTE_EXTEND) {
        enqueue_event (ctx->iw, IN_MOVED_TO, di);
    } else
#endif
    enqueue_event (ctx->iw, IN_CREATE, di);
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

#ifdef HAVE_NOTE_EXTEND_ON_SUBFILE_RENAME
    if (ctx->fflags & NOTE_EXTEND) {
        enqueue_event (ctx->iw, IN_MOVED_FROM, di);
    } else
#endif
    enqueue_event (ctx->iw, IN_DELETE, di);
    iwatch_del_subwatch (ctx->iw, di);
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

    iwatch_del_subwatch (ctx->iw, di);
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
    assert (((handle_context *) udata)->iw != NULL);

#ifdef HAVE_NOTE_EXTEND_ON_SUBFILE_RENAME
    handle_context *ctx = udata;
    if (ctx->fflags & NOTE_EXTEND) {
        handle_replaced (udata, from_di);
    } else
#endif
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

    if (to_di->type == S_IFUNK) {
        to_di->type = from_di->type;
    }

    enqueue_event (ctx->iw, IN_MOVED_FROM, from_di);
    enqueue_event (ctx->iw, IN_MOVED_TO, to_di);
}


static const traverse_cbs cbs = {
    handle_unchanged,
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

    DIR *dir;
    dep_list *was = NULL, *now = NULL;
    was = iw->deps;

#if READDIR_DOES_OPENDIR > 0
    dir = fdreopendir (iw->fd);
    if (dir == NULL) {
        if (errno == ENOENT) {
            /* Why do I skip ENOENT? Because the directory could be deleted
             * at this point */
            now = dl_create ();
            goto do_diff;
        }
        perror_msg ("Failed to reopen directory for listing");
        return;
    }
#else
    dir = iw->dir;
    rewinddir(dir);
#endif

    now = dl_readdir (dir);

#if READDIR_DOES_OPENDIR > 0
    closedir (dir);

do_diff:
#endif
    if (now == NULL) {
        perror_msg ("Failed to create a listing for watch %d", iw->wd);
        return;
    }

    iw->deps = now;

    handle_context ctx;
    memset (&ctx, 0, sizeof (ctx));
    ctx.iw = iw;
    ctx.fflags = event->fflags;

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
    /* Heuristic order of dearrgegated inotify events */
    static uint32_t ie_order[] = {
#ifdef NOTE_OPEN
        IN_OPEN,
#endif
#ifdef NOTE_READ
        IN_ACCESS,
#endif
        IN_MODIFY,
#ifdef NOTE_CLOSE
        IN_CLOSE_NOWRITE,
#endif
#ifdef NOTE_CLOSE_WRITE
        IN_CLOSE_WRITE,
#endif
        IN_ATTRIB,
        IN_MOVE_SELF,
        IN_DELETE_SELF,
        IN_UNMOUNT
    };

    assert (wrk != NULL);
    assert (event != NULL);

    watch *w = (watch *)event->udata;
    assert (w != NULL);
    assert (w->fd == event->ident);

    i_watch *iw = w->iw;
    assert (watch_set_find (&iw->watches, w->inode) == w);

    uint32_t flags = event->fflags;

    if (!(w->flags & WF_ISSUBWATCH)) {
        /* Set deleted flag if no more links exist */
        if (flags & NOTE_DELETE &&
            (!S_ISREG (w->flags) || is_deleted (w->fd))) {
                w->flags |= WF_DELETED;
        }

#if (READDIR_DOES_OPENDIR == 2) && \
    defined (NOTE_OPEN) && defined (NOTE_CLOSE)
        /* Mask events produced by open/closedir calls while directory diffing.
         * Kqueue coalesces both events as kevent is not called that time */
        if (w->flags & WF_SKIP_NEXT) {
            flags &= ~(NOTE_OPEN | NOTE_CLOSE);
        }
#endif
#ifdef NOTE_READ
        /* Mask event produced by readdir call while directory diffing. */
        if (w->flags & WF_SKIP_NEXT) {
            flags &= ~NOTE_READ;
        }
#endif
        if (S_ISDIR (w->flags)) {
            w->flags &= ~WF_SKIP_NEXT;
        }

        uint32_t i_flags = kqueue_to_inotify (flags, w->flags);

        size_t i;
        /* Deaggregate inotify events (most of) */
        for (i = 0; i < nitems (ie_order); i++) {
            if (i_flags & ie_order[i]) {
                /* Report deaggregated items */
                enqueue_event (iw,
                               ie_order[i] | (i_flags & ~IN_ALL_EVENTS),
                               NULL);
            } else
            /* Report subfiles(dependency) list changes */
            if (ie_order[i] == IN_MODIFY &&
                flags & NOTE_WRITE && S_ISDIR (w->flags)) {
#ifdef __OpenBSD__
                /* OpenBSD notifies user with kevent about file moved in/out
                 * watched directory slightly BEFORE change hits directory
                 * content. Workaround it with adding a small delay. */
                struct timespec timeout = { 0, 5 };
                nanosleep (&timeout, NULL);
#endif
                produce_directory_diff (iw, event);
                w->flags |= WF_SKIP_NEXT;
            }
        }

        if (w->flags & WF_DELETED || flags & NOTE_REVOKE) {
            iw->is_closed = 1;
        }
    } else {
        uint32_t i_flags = kqueue_to_inotify (flags, w->flags);

        size_t i;
        /* Deaggregate inotify events */
        for (i = 0; i < nitems (ie_order); i++) {
            if (i_flags & ie_order[i]) {
                dep_node *iter = NULL;
                /* Report deaggregated items */
                SLIST_FOREACH (iter, &iw->deps->head, next) {
                    dep_item *di = iter->item;

                    if (di->inode == w->inode) {
                        enqueue_event (iw,
                                       ie_order[i] | (i_flags & ~IN_ALL_EVENTS),
                                       di);
                    }
                }
            }
        }
    }

    if (iw->is_closed) {
        worker_remove (wrk, iw->wd);
    }
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
    worker_cmd *cmd;
    size_t i, sbspace = 0;
#define MAXEVENTS 32
    struct kevent received[MAXEVENTS];

    for (;;) {
        if (sbspace > 0 && wrk->eq.count > 0) {
            event_queue_flush (&wrk->eq, sbspace);
            sbspace = 0;
        }

        int nevents = kevent (wrk->kq, NULL, 0, received, MAXEVENTS, NULL);
        if (nevents == -1) {
            perror_msg ("kevent failed");
            continue;
        }
        for (i = 0; i < nevents; i++) {
            if (received[i].ident == wrk->io[KQUEUE_FD]) {
                if (received[i].flags & EV_EOF) {
                    wrk->io[INOTIFY_FD] = -1;
                    worker_erase (wrk);
                    /* Notify user threads waiting for cmd of grim news */
                    worker_post (wrk);
                    worker_free (wrk);
                    return NULL;

                } else if (received[i].filter == EVFILT_WRITE) {
                    sbspace = received[i].data;
                    if (sbspace >= wrk->sockbufsize) {
                        /* Tell event queue about empty communication pipe */
                        event_queue_reset_last(&wrk->eq);
                    }
#ifdef EVFILT_USER
                } else if (received[i].filter == EVFILT_USER) {
                    cmd = received[i].udata;
                    process_command (wrk, cmd);
#else
                } else if (received[i].filter == EVFILT_READ) {
                    safe_read (wrk->io[KQUEUE_FD], &cmd, sizeof (cmd));
                    process_command (wrk, cmd);
#endif
                }
            } else {
                produce_notifications (wrk, &received[i]);
            }
        }
    }
    return NULL;
}
