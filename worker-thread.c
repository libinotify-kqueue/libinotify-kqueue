/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
  Copyright (c) 2024 Serenity Cyber Security, LLC
                     Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <sys/event.h>

#include <stddef.h> /* NULL */
#include <assert.h>
#include <errno.h>  /* errno */
#include <stdlib.h> /* calloc, realloc */
#include <string.h> /* memset */
#include <stdio.h>
#include <unistd.h>

#include "sys/inotify.h"

#include "compat.h"
#include "config.h"
#include "dep-list.h"
#include "inotify-watch.h"
#include "utils.h"
#include "watch.h"
#include "worker.h"

void worker_erase (struct worker *wrk);
static void handle_moved (void *udata,
                          struct dep_item *from_di,
                          struct dep_item *to_di);

/**
 * Create a new inotify event and place it to event queue.
 *
 * @param[in] iw   A pointer to #i_watch.
 * @param[in] mask An inotify watch mask.
 * @param[in] di   A pointer to dependency item for subfiles (NULL for user).
 * @return 0 on success, -1 otherwise.
 **/
static int
enqueue_event (struct i_watch *iw, uint32_t mask, const struct dep_item *di)
{
    const char *name = NULL;
    uint32_t cookie = 0;

    assert (iw != NULL);
    assert (iw->wrk != NULL);

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
        iw->is_closed = true;
    }

    if (di != DI_PARENT) {
        name = di->path;
        if (mask & IN_MOVE) {
            cookie = di->inode & 0x00000000FFFFFFFF;
        }
        if (S_ISDIR (di->type)) {
            mask |= IN_ISDIR;
        }
    }

    if (event_queue_enqueue (&iw->wrk->eq, iw->wd, mask, cookie, name) == -1) {
        perror_msg (("Failed to enqueue a inotify event %x", mask));
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
process_command (struct worker *wrk, struct worker_cmd *cmd)
{
    assert (wrk != NULL);

    switch (cmd->type) {
    case WCMD_ADD:
        cmd->retval = worker_add_or_modify (wrk,
                                            cmd->cmd.add.filename,
                                            cmd->cmd.add.mask);
        cmd->error = errno;
        break;
    case WCMD_REMOVE:
        cmd->retval = worker_remove (wrk, cmd->cmd.rm_id);
        cmd->error = errno;
        break;
    case WCMD_PARAM:
        cmd->retval = worker_set_param (wrk,
                                        cmd->cmd.param.param,
                                        cmd->cmd.param.value);
        cmd->error = errno;
        break;
    default:
        perror_msg (("Worker processing a command without a command - "
                    "something went wrong."));
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
struct handle_context {
    struct i_watch *iw;
    uint32_t fflags;
};

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
handle_added (void *udata, struct dep_item *di)
{
    struct handle_context *ctx = (struct handle_context *) udata;

    assert (ctx != NULL);
    assert (ctx->iw != NULL);

    iwatch_add_subwatch (ctx->iw, di);
#ifdef HAVE_NOTE_EXTEND_ON_MOVE_TO
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
handle_removed (void *udata, struct dep_item *di)
{
    struct handle_context *ctx = (struct handle_context *) udata;

    assert (ctx != NULL);
    assert (ctx->iw != NULL);

#ifdef HAVE_NOTE_EXTEND_ON_MOVE_FROM
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
handle_replaced (void *udata, struct dep_item *di)
{
    struct handle_context *ctx = (struct handle_context *) udata;

    assert (ctx != NULL);
    assert (ctx->iw != NULL);

    iwatch_del_subwatch (ctx->iw, di);
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
handle_moved (void *udata, struct dep_item *from_di, struct dep_item *to_di)
{
    struct handle_context *ctx = (struct handle_context *) udata;

    assert (ctx != NULL);
    assert (ctx->iw != NULL);

    if (S_ISUNK (to_di->type)) {
        di_settype (to_di, from_di->type);
    }

    enqueue_event (ctx->iw, IN_MOVED_FROM, from_di);
    enqueue_event (ctx->iw, IN_MOVED_TO, to_di);
    iwatch_move_subwatch (ctx->iw, from_di, to_di);
}


static const struct traverse_cbs cbs = {
    handle_added,
    handle_removed,
    handle_replaced,
    handle_moved,
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
produce_directory_diff (struct i_watch *iw, struct kevent *event)
{
    struct handle_context ctx;
    struct chg_list *changes;

    assert (iw != NULL);
    assert (event != NULL);

    changes = dl_listing (iw->fd, &iw->deps);
    if (changes == NULL) {
        perror_msg (("Failed to create a listing for watch %d", iw->wd));
        return;
    }

    memset (&ctx, 0, sizeof (ctx));
    ctx.iw = iw;
    ctx.fflags = event->fflags;

    dl_calculate (&iw->deps, changes, &cbs, &ctx);
}

/**
 * Produce notifications about file system activity observer by a worker.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] event A pointer to the associated received kqueue event.
 **/
void
produce_notifications (struct worker *wrk, struct kevent *event)
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

    struct watch *w;
    struct watch_dep *wd, *wd2;
    uint32_t i_flags_par, i_flags_chl;
    uint32_t flags;
    bool deleted = false;
    bool reiterate;
    mode_t mode;
    size_t i;

    assert (wrk != NULL);
    assert (event != NULL);

    w = (struct watch *)event->udata;
    assert (w != NULL);
    assert (w->fd == event->ident);
    assert (!watch_deps_empty (w));

    flags = event->fflags;
    mode = watch_get_mode (w);

    /* Set deleted flag if no more links exist */
    if (flags & NOTE_DELETE && (!S_ISREG (mode) || is_deleted (w->fd))) {
        deleted = true;
    }

    /* Mask events produced by opendir, readdir and closedir calls while
     * directory diffing. Kqueue always aggregates all 3 events into single
     * event as working thread is not calling kevent() that time. */
    if (w->skip_next) {
#if defined (NOTE_OPEN) && (READDIR_DOES_OPENDIR == 2)
        flags &= ~NOTE_OPEN;
#endif
#if defined (NOTE_READ)
        flags &= ~NOTE_READ;
#endif
#if defined (NOTE_CLOSE) && (READDIR_DOES_OPENDIR == 2)
        flags &= ~NOTE_CLOSE;
#endif
        w->skip_next = false;
    }

    i_flags_par = kqueue_to_inotify (flags, mode, true, deleted);
    i_flags_chl = kqueue_to_inotify (flags, mode, false, deleted);

    /* Deaggregate inotify events  */
    for (i = 0; i < nitems (ie_order); i++) {

        WD_FOREACH (wd, w) {

            struct i_watch *iw = wd->iw;
            bool is_parent = watch_dep_is_parent (wd);
            uint32_t i_flags = is_parent ? i_flags_par : i_flags_chl;

            assert (watch_set_find (&wrk->watches, watch_get_dev (w), watch_get_inode (w)) == w);
            assert ((mode & S_IFMT) == (watch_dep_get_mode (wd) & S_IFMT));
            assert (watch_get_inode (w) == watch_dep_get_inode (wd));
            assert (is_parent || wd->di == dl_find (&iw->deps, wd->di->path));

            if (is_parent && ie_order[i] == IN_MODIFY &&
                flags & NOTE_WRITE && S_ISDIR (iw->mode)) {
#ifdef __OpenBSD__
                /* OpenBSD notifies user with kevent about file moved in/out
                 * watched directory slightly BEFORE change hits directory
                 * content. Workaround it with adding a small delay. */
                {
                    struct timespec timeout = { 0, 5 };
                    nanosleep (&timeout, NULL);
                }
#endif
                produce_directory_diff (iw, event);
                w->skip_next = true;

            } else if (i_flags & ie_order[i]) {

                /* Report deaggregated items */
                enqueue_event (iw,
                               ie_order[i] | (i_flags & ~IN_ALL_EVENTS),
                               wd->di);
            }
        }
    }

   /* worker_remove can free watch deps and watch itself on return so we should
    * reiterate after worker_remove or break loop if watch is associated with
    * only one inotify watch to avoid use after free */
    do {
        reiterate = false;
        WD_FOREACH (wd, w) {
            if (wd->iw->is_closed || (watch_dep_is_parent (wd) &&
                (deleted || flags & NOTE_REVOKE))) {
                /* Check are 2 or more #i_watch associated with #watch */
                WD_FOREACH (wd2, w) {
                    if (wd->iw != wd2->iw) {
                        reiterate = true;
                        break;
                    }
                }
                worker_remove_iwatch (wrk, wd->iw);
                break;
            }
        }
    } while (reiterate);
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
    struct worker* wrk = (struct worker *) arg;
    struct worker_cmd *cmd;
#define SBEMPTY SIZE_MAX
    size_t sbspace = SBEMPTY;
#define MAXEVENTS 1
    struct kevent received[MAXEVENTS];
    bool direct = wrk->io[KQUEUE_FD] == wrk->io[INOTIFY_FD];

    assert (wrk != NULL);

    for (;;) {
        size_t i;
        int nevents;

        if (sbspace > 0 && wrk->eq.mem_events > 0) {
            ssize_t sent;
            if (sbspace == SBEMPTY && !direct) {
                /* Try to track sockbufsize changes on the fly */
                sbspace = wrk->sockbufsize;
            }
            sent = event_queue_flush (&wrk->eq, sbspace);
            if (sent < 0) {
                if (errno == EPIPE || errno == EBADF || errno == ENOTSOCK) {
                    goto die;
                } else {
                    sent = 0; /* Ignore nonfatal errors */
                }
            }
            if (!direct)
                sbspace = wrk->eq.mem_events == 0 ? sbspace - sent : 0;
        }

        nevents = kevent (wrk->kq, NULL, 0, received, MAXEVENTS, NULL);
        if (nevents == -1) {
            perror_msg (("kevent failed"));
            continue;
        }
        for (i = 0; i < nevents; i++) {
            if (received[i].ident == wrk->io[KQUEUE_FD]) {
                if (received[i].flags & EV_EOF) {
                    goto die;
#ifdef EVFILT_EMPTY
                } else if (received[i].filter == EVFILT_EMPTY) {
#else
                } else if (received[i].filter == EVFILT_WRITE) {
                    assert (received[i].data >= wrk->sockbufsize);
#endif
                    sbspace = SBEMPTY;
                    /* Tell event queue about empty communication pipe */
                    event_queue_reset_last (&wrk->eq);
#ifdef EVFILT_USER
                } else if (received[i].filter == EVFILT_USER) {
#ifdef __DragonFly__
                    /* DragonflyBSD does not copy udata */
                    cmd = (struct worker_cmd *)(intptr_t)received[i].data;
#else
                    cmd = received[i].udata;
#endif
                    if (cmd->type != WCMD_CLOSE)
                        process_command (wrk, cmd);
                    else
                        goto die;
#else
                } else if (received[i].filter == EVFILT_READ) {
                    if (read (wrk->io[KQUEUE_FD], &cmd, sizeof (cmd)) != -1) {
                        process_command (wrk, cmd);
                    }
#endif
                }
            } else {
                produce_notifications (wrk, &received[i]);
            }
        }
    }
die:
    worker_erase (wrk);
    /* Notify user threads waiting for cmd of grim news */
    worker_post (wrk);
    worker_free (wrk);
    return NULL;
}
