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
#include <unistd.h> /* write */
#include <stdlib.h> /* calloc, realloc */
#include <string.h> /* memset */
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"

#include "utils.h"
#include "conversions.h"
#include "worker.h"
#include "worker-sets.h"
#include "worker-thread.h"

void worker_erase (worker *wrk);

/**
 * This structure represents a sequence of packets.
 * It is used to accumulate inotify events in a single piece
 * of memory and to write it via a single call to write().
 * 
 * Such accumulation is needed when processing directory changes and
 * a lot of events are expected to be sent to the user.
 **/
typedef struct bulk_events {
    void *memory;
    size_t size;
} bulk_events;

/**
 * Write a packet to the buffer. Extend the buffer, if needed.
 *
 * @param[in] be  A pointer to #bulk_events.
 * @param[in] mem A pointer to data to write.
 * @param[in] size The number of bytes to write.
 **/
int
bulk_write (bulk_events *be, void *mem, size_t size)
{
    assert (be != NULL);
    assert (mem != NULL);

    void *ptr = realloc (be->memory, be->size + size);
    if (ptr == NULL) {
        perror_msg ("Failed to extend the bulk events buffer on %d bytes", 
                    size);
        return -1;
    }

    be->memory = ptr;
    memcpy ((char *)be->memory + be->size, mem, size);
    be->size += size;
    return 0;
}

/**
 * Check if a file under given path is/was a directory. Use worker's
 * cached data (watches) to query file type (this function is called
 * when something happens in a watched directory, so we SHOULD have
 * a watch for its contents
 *
 * @param[in] path A file path
 * @param[in] wrk A worker instance for which a change has been triggered.
 *
 * @return 1 if dir (cached), 0 otherwise.
 **/
static int
check_is_dir_cached (const char *path, worker *wrk)
{
    int i;
    for (i = 0; i < wrk->sets.length; i++) {
        const watch *w = wrk->sets.watches[i];
        if (w != NULL && strcmp (path, w->filename) == 0 && w->is_really_dir)
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
    worker *wrk;
    watch *w;
    bulk_events *be;
} handle_context;

/**
 * Produce an IN_CREATE notification for a new file and start wathing on it.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] path   File name of a new file.
 * @param[in] inode  Inode number of a new file.
 **/
static void
handle_added (void *udata, const char *path, ino_t inode)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);
    assert (ctx->be != NULL);

    struct inotify_event *ie = NULL;
    int ie_len = 0;

    ie = create_inotify_event (ctx->w->fd, IN_CREATE, 0, path, &ie_len);
    if (ie == NULL) {
        perror_msg ("Failed to create an IN_CREATE event for %s", path);
        return;
    }

    char *npath = path_concat (ctx->w->filename, path);
    if (npath != NULL) {
        watch *neww = worker_start_watching (ctx->wrk, npath, path, ctx->w->flags, WATCH_DEPENDENCY);
        if (neww == NULL) {
            perror_msg ("Failed to start watching on a new dependency %s", npath);
        } else {
            neww->parent = ctx->w;
            if (neww->is_really_dir) {
                ie->mask |= IN_ISDIR;
            }
        }
        free (npath);
    } else {
        perror_msg ("Failed to allocate a path to start watching a dependency");
    }

    bulk_write (ctx->be, ie, ie_len);
    free (ie);
}

/**
 * Produce an IN_DELETE notification for a removed file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] path   File name of the removed file.
 * @param[in] inode  Inode number of the removed file.
 **/
static void
handle_removed (void *udata, const char *path, ino_t inode)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);
    assert (ctx->be != NULL);

    struct inotify_event *ie = NULL;
    int ie_len = 0;
    int addMask = check_is_dir_cached (path, ctx->wrk) ? IN_ISDIR : 0;

    ie = create_inotify_event (ctx->w->fd, IN_DELETE | addMask, 0, path, &ie_len);
    if (ie != NULL) {
        bulk_write (ctx->be, ie, ie_len);
        free (ie);
    } else {
        perror_msg ("Failed to create an IN_DELETE event for %s", path);
    }
}

/**
 * Produce an IN_MOVED_FROM/IN_MOVED_TO notifications pair for a replaced file.
 * Also stops wathing on the replaced file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata       A pointer to user data (#handle_context).
 * @param[in] from_path   File name of the source file.
 * @param[in] from_inode  Inode number of the source file.
 * @param[in] to_path     File name of the replaced file.
 * @param[in] to_inode    Inode number of the replaced file.
**/
static void
handle_replaced (void       *udata,
                 const char *from_path,
                 ino_t       from_inode,
                 const char *to_path,
                 ino_t       to_inode)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);
    assert (ctx->be != NULL);

    uint32_t cookie = from_inode & 0x00000000FFFFFFFF;
    int event_len = 0;
    int addMask = check_is_dir_cached (from_path, ctx->wrk) ? IN_ISDIR : 0;
    struct inotify_event *ev;

    ev = create_inotify_event (ctx->w->fd, IN_MOVED_FROM | addMask, cookie,
                               from_path, &event_len);
    if (ev != NULL) {
        bulk_write (ctx->be, ev, event_len);
        free (ev);
    }  else {
        perror_msg ("Failed to create an IN_MOVED_FROM event (*) for %s",
                    from_path);
    }

    ev = create_inotify_event (ctx->w->fd, IN_MOVED_TO | addMask, cookie,
                               to_path, &event_len);
    if (ev != NULL) {
        bulk_write (ctx->be, ev, event_len);
        free (ev);
    } else {
        perror_msg ("Failed to create an IN_MOVED_TO event (*) for %s",
                    to_path);
    }

    int i;
    for (i = 1; i < ctx->wrk->sets.length; i++) {
        watch *iw = ctx->wrk->sets.watches[i];
        if (iw && iw->parent == ctx->w && strcmp (to_path, iw->filename) == 0) {
            dep_list *dl = dl_create (iw->filename, iw->inode);
            worker_remove_many (ctx->wrk, ctx->w, dl, 0);
            dl_shallow_free (dl);
            break;
        }
    }
}


/**
 * Produce an IN_DELETE/IN_CREATE notifications pair for an overwritten file.
 * Reopen a watch for the overwritten file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] path   File name of the overwritten file.
 * @param[in] inode  Inode number of the overwritten file.
 **/
static void
handle_overwritten (void *udata, const char *path, ino_t inode)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);
    assert (ctx->be != NULL);

    int addMask = check_is_dir_cached (path, ctx->wrk) ? IN_ISDIR : 0;
    int i;
    for (i = 0; i < ctx->wrk->sets.length; i++) {
        watch *wi = ctx->wrk->sets.watches[i];
        if (wi && (strcmp (wi->filename, path) == 0)
            && wi->parent == ctx->w) {
            if (watch_reopen (wi) == -1) {
                /* I dont know, what to do */
                /* Not a very beautiful way to remove a single dependency */
                dep_list *dl = dl_create (wi->filename, wi->inode);
                worker_remove_many (ctx->wrk, ctx->w, dl, 0);
                dl_shallow_free (dl);
            } else {
                uint32_t cookie = inode & 0x00000000FFFFFFFF;
                int event_len = 0;
                struct inotify_event *ev;

                ev = create_inotify_event (ctx->w->fd, IN_DELETE | addMask, cookie,
                                           path,
                                           &event_len);
                if (ev != NULL) {
                    bulk_write (ctx->be, ev, event_len);
                    free (ev);
                }  else {
                    perror_msg ("Failed to create an IN_DELETE event (*) for %s",
                                path);
                }

                /* TODO: Could a file be overwritten by a directory? What will happen to
                 * existing watch in this case? Repoen? */
                ev = create_inotify_event (ctx->w->fd, IN_CREATE, cookie,
                                           path,
                                           &event_len);
                if (ev != NULL) {
                    bulk_write (ctx->be, ev, event_len);
                    free (ev);
                } else {
                    perror_msg ("Failed to create an IN_CREATE event (*) for %s",
                                path);
                }
            }
            break;
        }
    }
}

/**
 * Produce an IN_MOVED_FROM/IN_MOVED_TO notifications pair for a renamed file.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata       A pointer to user data (#handle_context).
 * @param[in] from_path   The old name of the file.
 * @param[in] from_inode  Inode number of the old file.
 * @param[in] to_path     The new name of the file.
 * @param[in] to_inode    Inode number of the new file.
**/
static void
handle_moved (void       *udata,
              const char *from_path,
              ino_t       from_inode,
              const char *to_path,
              ino_t       to_inode)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);
    assert (ctx->be != NULL);

    int addMask = check_is_dir_cached (from_path, ctx->wrk) ? IN_ISDIR : 0;
    uint32_t cookie = from_inode & 0x00000000FFFFFFFF;
    int event_len = 0;
    struct inotify_event *ev;

    ev = create_inotify_event (ctx->w->fd, IN_MOVED_FROM | addMask, cookie,
                               from_path, &event_len);
    if (ev != NULL) {   
        bulk_write (ctx->be, ev, event_len);
        free (ev);
    } else {
        perror_msg ("Failed to create an IN_MOVED_FROM event for %s",
                    from_path);
    }
    
    ev = create_inotify_event (ctx->w->fd, IN_MOVED_TO | addMask, cookie,
                               to_path, &event_len);
    if (ev != NULL) {   
        bulk_write (ctx->be, ev, event_len);
        free (ev);
    } else {
        perror_msg ("Failed to create an IN_MOVED_TO event for %s",
                    to_path);
    }
}

/**
 * Remove the appropriate watches if the files were removed from the directory.
 * 
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 * @param[in] list   A list of the removed files. 
 **/
static void
handle_many_removed (void *udata, const dep_list *list)
{
    assert (udata != NULL);
    handle_context *ctx = (handle_context *) udata;

    if (list) {
        worker_remove_many (ctx->wrk, ctx->w, list, 0);
    }
}

/**
 * Update file names for the renamed files.
 *
 * This function is used as a callback and is invoked from the dep-list
 * routines.
 *
 * @param[in] udata  A pointer to user data (#handle_context).
 **/
static void
handle_names_updated (void *udata)
{
    assert (udata != NULL);

    handle_context *ctx = (handle_context *) udata;
    assert (ctx->wrk != NULL);
    assert (ctx->w != NULL);

    worker_update_paths (ctx->wrk, ctx->w);
}


static const traverse_cbs cbs = {
    handle_added,
    handle_removed,
    handle_replaced,
    handle_overwritten,
    handle_moved,
    NULL, /* many_added */
    handle_many_removed,
    handle_names_updated,
};

/**
 * Detect and notify about the changes in the watched directory.
 *
 * This function is top-level and it operates with other specific routines
 * to notify about different sets of events in a different conditions.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] w     A pointer to #watch.
 * @param[in] event A pointer to the received kqueue event.
 **/
void
produce_directory_diff (worker *wrk, watch *w, struct kevent *event)
{
    assert (wrk != NULL);
    assert (w != NULL);
    assert (event != NULL);

    assert (w->type == WATCH_USER);
    assert (w->is_directory);

    dep_list *was = NULL, *now = NULL;
    int failed = 0;
    was = dl_shallow_copy (w->deps);
    now = dl_listing (w->filename, &failed);

    if (now == NULL && failed && errno != ENOENT) {
        /* Why do I skip ENOENT? Because the directory could be deleted at this
         * point */
        perror_msg ("Failed to create a listing for directory %s",
                    w->filename);
        dl_shallow_free (was);
        printf("Bye!\n");
        return;
    }

    dl_shallow_free (w->deps);
    w->deps = now;

    bulk_events be;
    memset (&be, 0, sizeof (be));

    handle_context ctx;
    memset (&ctx, 0, sizeof (ctx));
    ctx.wrk = wrk;
    ctx.w = w;
    ctx.be = &be;
    
    dl_calculate (was, now, &cbs, &ctx);
    
    if (be.memory) {
        safe_write (wrk->io[KQUEUE_FD], be.memory, be.size);
        free (be.memory);
    }

    dl_free (was);
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

    watch *w = wrk->sets.watches[UDATA_TO_INDEX (event->udata)];

    uint32_t flags = event->fflags;

    if (w->type == WATCH_USER) {
        if (w->is_directory
            && (flags & (NOTE_WRITE | NOTE_EXTEND))
            && (w->flags & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO))) {
            produce_directory_diff (wrk, w, event);
            flags &= ~(NOTE_WRITE | NOTE_EXTEND);
        }

        if (flags) {
            struct inotify_event *ie = NULL;
            int ev_len;
            ie = create_inotify_event (w->fd,
                                       kqueue_to_inotify (flags, w->is_really_dir, 0),
                                       0,
                                       NULL,
                                       &ev_len);
            if (ie != NULL) {
                safe_write (wrk->io[KQUEUE_FD], ie, ev_len);
                free (ie);
            } else {
                perror_msg ("Failed to create a new inotify event");
            }

            if ((flags & NOTE_DELETE) && w->flags & IN_DELETE_SELF) {
                /* TODO: really look on IN_DETELE_SELF? */
                ie = create_inotify_event (w->fd, IN_IGNORED, 0, NULL, &ev_len);
                if (ie != NULL) {
                    safe_write (wrk->io[KQUEUE_FD], ie, ev_len);
                    free (ie);
                } else {
                    perror_msg ("Failed to create a new IN_IGNORED event on remove");
                }
            }
        }
    } else {
        /* for dependency events, ignore some notifications */
        if (event->fflags & (NOTE_ATTRIB | NOTE_LINK | NOTE_WRITE | NOTE_EXTEND)) {
            struct inotify_event *ie = NULL;
            watch *p = w->parent;
            assert (p != NULL);
            int ev_len;
            ie = create_inotify_event
                (p->fd,
                 kqueue_to_inotify (flags, w->is_really_dir, 1),
                 0,
                 w->filename,
                 &ev_len);

            if (ie != NULL) {
                safe_write (wrk->io[KQUEUE_FD], ie, ev_len);
                free (ie);
            } else {
                perror_msg ("Failed to create a new inotify event for dependency");
            }
        }
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

    for (;;) {
        struct kevent received;

        int ret = kevent (wrk->kq,
                          wrk->sets.events,
                          wrk->sets.length,
                          &received,
                          1,
                          NULL);
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
