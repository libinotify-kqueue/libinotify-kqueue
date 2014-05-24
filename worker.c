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

#include <stdlib.h>
#include <string.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
#include <assert.h>
#include <stdio.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>

#include "sys/inotify.h"

#include "utils.h"
#include "conversions.h"
#include "worker-thread.h"
#include "worker.h"

static void
worker_update_flags (worker *wrk, watch *w, uint32_t flags);

static void
worker_cmd_reset (worker_cmd *cmd);

/**
 * Initialize a command with the data of the inotify_add_watch() call.
 *
 * @param[in] cmd      A pointer to #worker_cmd.
 * @param[in] filename A file name of the watched entry.
 * @param[in] mask     A combination of the inotify watch flags.
 **/
void
worker_cmd_add (worker_cmd *cmd, const char *filename, uint32_t mask)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_ADD;
    cmd->add.filename = strdup (filename);
    cmd->add.mask = mask;

    ik_barrier_init (&cmd->sync, 2);
}


/**
 * Initiailize a command with the data of the inotify_rm_watch() call.
 *
 * @param[in] cmd       A pointer to #worker_cmd
 * @param[in] watch_id  The identificator of a watch to remove.
 **/
void
worker_cmd_remove (worker_cmd *cmd, int watch_id)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_REMOVE;
    cmd->rm_id = watch_id;

    ik_barrier_init (&cmd->sync, 2);
}

/**
 * Reset the worker command.
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
static void
worker_cmd_reset (worker_cmd *cmd)
{
    assert (cmd != NULL);

    if (cmd->type == WCMD_ADD) {
        free (cmd->add.filename);
    }
    memset (cmd, 0, sizeof (worker_cmd));
}

/**
 * Wait on a worker command.
 *
 * This function is used by both user and worker threads for
 * synchronization.
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_cmd_wait (worker_cmd *cmd)
{
    assert (cmd != NULL);
    ik_barrier_wait (&cmd->sync);
}

/**
 * Release a worker command.
 *
 * This function releases resources associated with worker command.
 * This function must be called after a successfull worker_cmd_wait()
 * and only by a single user of worker_cmd (an initiator).
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_cmd_release (worker_cmd *cmd)
{
    assert (cmd != NULL);
    ik_barrier_destroy (&cmd->sync);
}



/**
 * Create a new worker and start its thread.
 *
 * @return A pointer to a new worker.
 **/
worker*
worker_create ()
{
    pthread_attr_t attr;
    worker* wrk = calloc (1, sizeof (worker));

    if (wrk == NULL) {
        perror_msg ("Failed to create a new worker");
        goto failure;
    }

    wrk->kq = kqueue ();
    if (wrk->kq == -1) {
        perror_msg ("Failed to create a new kqueue");
        goto failure;
    }

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, (int *) wrk->io) == -1) {
        perror_msg ("Failed to create a socket pair");
        goto failure;
    }

    if (worker_sets_init (&wrk->sets, wrk->io[KQUEUE_FD]) == -1) {
        goto failure;
    }
    pthread_mutex_init (&wrk->mutex, NULL);

    /* create a run a worker thread */
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create (&wrk->thread, &attr, worker_thread, wrk) != 0) {
        perror_msg ("Failed to start a new worker thread");
        goto failure;
    }

    wrk->closed = 0;
    return wrk;
    
failure:
    if (wrk != NULL) {
        worker_free (wrk);
    }
    return NULL;
}

/**
 * Free a worker and all the associated memory.
 *
 * @param[in] wrk A pointer to #worker.
 **/
void
worker_free (worker *wrk)
{
    assert (wrk != NULL);

    close (wrk->io[KQUEUE_FD]);
    wrk->io[KQUEUE_FD] = -1;

    close (wrk->kq);
    wrk->closed = 1;

    worker_cmd_reset (&wrk->cmd);
    worker_sets_free (&wrk->sets);
    pthread_mutex_destroy (&wrk->mutex);

    free (wrk);
}

/**
 * When starting watching a directory, start also watching its contents.
 *
 * This function creates and initializes additional watches for a directory.
 *
 * @param[in] wrk    A pointer to #worker.
 * @param[in] event  A pointer to the associated kqueue event.
 * @param[in] parent A pointer to the parent #watch, i.e. the watch we add
 *     dependencies for.
 * @return 0 on success, -1 otherwise.
 **/
static int
worker_add_dependencies (worker        *wrk,
                         struct kevent *event,
                         watch         *parent)
{
    assert (wrk != NULL);
    assert (parent != NULL);
    assert (parent->type == WATCH_USER);
    assert (event != NULL);

    parent->deps = dl_listing (parent->filename);

    {   dep_list *iter = parent->deps;
        while (iter != NULL) {
            char *path = path_concat (parent->filename, iter->path);
            if (path != NULL) {
                watch *neww = worker_start_watching (wrk,
                                                     path,
                                                     iter->path,
                                                     parent->flags,
                                                     WATCH_DEPENDENCY);
                if (neww == NULL) {
                    perror_msg ("Failed to start watching a dependency %s of %s",
                                path,
                                iter->path);
                } else {
                    neww->parent = parent;
                }
                free (path);
            } else {
                perror_msg ("Failed to allocate a path while adding a dependency");
            }
            iter = iter->next;
        }
    }
    return 0;
}

/**
 * Start watching a file or a directory.
 *
 * @param[in] wrk        A pointer to #worker.
 * @param[in] path       Path to watch.
 * @param[in] entry_name Entry name. Used for dependencies.
 * @param[in] flags      A combination of inotify event flags.
 * @param[in] type       The type of a watch.
 * @return A pointer to a created watch.
 **/
watch*
worker_start_watching (worker      *wrk,
                       const char  *path,
                       const char  *entry_name,
                       uint32_t     flags,
                       watch_type_t type)
{
    assert (wrk != NULL);
    assert (path != NULL);

    int i;

    if (worker_sets_extend (&wrk->sets, 1) == -1) {
        perror_msg ("Failed to extend worker sets");
        return NULL;
    }

    i = wrk->sets.length;
    wrk->sets.watches[i] = calloc (1, sizeof (struct watch));
    if (watch_init (wrk->sets.watches[i],
                    type,
                    &wrk->sets.events[i],
                    path,
                    entry_name,
                    flags,
                    i)
        == -1) {
        watch_free (wrk->sets.watches[i]);
        wrk->sets.watches[i] = NULL;
        return NULL;
    }
    ++wrk->sets.length;

    if (type == WATCH_USER && wrk->sets.watches[i]->is_directory) {
        worker_add_dependencies (wrk, &wrk->sets.events[i], wrk->sets.watches[i]);
    }
    return wrk->sets.watches[i];
}

/**
 * Add or modify a watch.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] path  A file path to watch.
 * @param[in] flags A combination of inotify watch flags.
 * @return An id of an added watch on success, -1 on failure.
**/
int
worker_add_or_modify (worker     *wrk,
                      const char *path,
                      uint32_t    flags)
{
    assert (path != NULL);
    assert (wrk != NULL);

    worker_sets *sets = &wrk->sets;

    assert (sets->events != NULL);
    assert (sets->watches != NULL);

    /* look up for an entry with this filename */
    size_t i = 0;
    for (i = 1; i < sets->length; i++) {
        const char *evpath = sets->watches[i]->filename;
        assert (evpath != NULL);

        if (sets->watches[i]->type == WATCH_USER &&
            strcmp (path, evpath) == 0) {
            worker_update_flags (wrk, sets->watches[i], flags);
            return sets->watches[i]->fd;
        }
    }

    /* add a new entry if path is not found */
    watch *w = worker_start_watching (wrk, path, NULL, flags, WATCH_USER);
    return (w != NULL) ? w->fd : -1;
}

/**
 * Stop and remove a watch.
 *
 * @param[in] wrk A pointer to #worker.
 * @param[in] id  An ID of the watch to remove.
 * @return 0 on success, -1 of failure.
 **/
int
worker_remove (worker *wrk,
               int     id)
{
    assert (wrk != NULL);
    assert (id != -1);

    size_t i;
    for (i = 1; i < wrk->sets.length; i++) {
        if (wrk->sets.events[i].ident == id) {
            int ie_len = 0;
            struct inotify_event *ie;
            ie = create_inotify_event (id, IN_IGNORED, 0, "", &ie_len);

            worker_remove_many (wrk,
                                wrk->sets.watches[i],
                                wrk->sets.watches[i]->deps,
                                1);

            if (ie != NULL) {
                safe_write (wrk->io[KQUEUE_FD], ie, ie_len);
                free (ie);
            } else {
                perror_msg ("Failed to create an IN_IGNORED event on stopping a watch");
            }
            break;
        }
    }
    /* Assume always success */
    return 0;
}


/**
 * Update watch flags.
 *
 * When called for a directory watch, update also the flags of all the
 * dependent (child) watches.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] w     A pointer to #watch.
 * @param[in] flags A combination of the inotify watch flags.
 **/
static void
worker_update_flags (worker *wrk, watch *w, uint32_t flags)
{
    assert (w != NULL);
    assert (w->event != NULL);

    w->flags = flags;
    w->event->fflags = inotify_to_kqueue (flags, w->is_directory);

    /* Propagate the flag changes also on all dependent watches */
    if (w->deps) {
        uint32_t ino_flags = inotify_to_kqueue (flags, 0);

        /* Yes, it is quite stupid to iterate over ALL watches of a worker
         * while we have a linked list of its dependencies.
         * TODO improve it */
        size_t i;
        for (i = 1; i < wrk->sets.length; i++) {
            watch *depw = wrk->sets.watches[i];
            if (depw->parent == w) {
                depw->flags = flags;
                depw->event->fflags = ino_flags;
            }
        }
    }
}

/**
 * Remove a list of watches, probably with their parent watch.
 *
 * @param[in] wrk     A pointer to #worker.
 * @param[in] parent  A pointer to the parent #watch.
 * @param[in] items   A list of watches to remove. All items must be childs of
 *     of the specified parent.
 * @param[in] remove_self Set to 1 to remove the parent watch too.
 **/
void
worker_remove_many (worker *wrk, watch *parent, const dep_list *items, int remove_self)
{
    assert (wrk != NULL);
    assert (parent != NULL);

    dep_list *to_remove = dl_shallow_copy (items);
    dep_list *to_head = to_remove;
    
    size_t i, j;

    for (i = 1, j = 1; i < wrk->sets.length; i++) {
        dep_list *iter = to_head;
        dep_list *prev = NULL;
        watch *w = wrk->sets.watches[i];

        if (remove_self && w == parent) {
            /* Remove the parent watch itself. The watch will be freed later,
             * now just remove it from the array */
            continue;
        }

        if (w->parent == parent) {
            while (iter != NULL && strcmp (iter->path, w->filename) != 0) {
                prev = iter;
                iter = iter->next;
            }

            if (iter != NULL) {
                /* At first, remove this entry from a list of files to remove */
                if (prev) {
                    prev->next = iter->next;
                } else {
                    to_head = iter->next;
                }

                /* Then, remove the watch itself */
                watch_free (w);
                continue;
            }
        }

        /* If the control reached here, keep this item */
        if (i != j) {
            wrk->sets.events[j] = wrk->sets.events[i];
            wrk->sets.events[j].udata = INDEX_TO_UDATA (j);
            wrk->sets.watches[j] = w;
            wrk->sets.watches[j]->event = &wrk->sets.events[j];
        }
        ++j;
    }

    if (remove_self) {
        watch_free (parent);
    }

    wrk->sets.length -= (i - j);

    for (i = wrk->sets.length; i < wrk->sets.allocated; i++) {
        wrk->sets.watches[i] = NULL;
    }

    dl_shallow_free (to_remove);
}

/**
 * Update paths of child watches for a specified watch.
 *
 * It is necessary when renames in the watched directory occur.
 *
 * @param[in] wrk    A pointer to #worker.
 * @param[in] parent A pointer to parent #watch.
 **/
void
worker_update_paths (worker *wrk, watch *parent)
{
    assert (wrk != NULL);
    assert (parent != NULL);

    if (parent->deps == NULL) {
        return;
    }

    dep_list *to_update = dl_shallow_copy (parent->deps);
    dep_list *to_head = to_update;
    size_t i, j;

    for (i = 1, j = 1; i < wrk->sets.length; i++) {
        dep_list *iter = to_head;
        dep_list *prev = NULL;
        watch *w = wrk->sets.watches[i];

        if (to_head == NULL) {
            break;
        }

        if (w->parent == parent) {
            while (iter != NULL && iter->inode != w->inode) {
                prev = iter;
                iter = iter->next;
            }

            if (iter != NULL) {
                if (prev) {
                    prev->next = iter->next;
                } else {
                    to_head = iter->next;
                }

                if (strcmp (iter->path, w->filename)) {
                    free (w->filename);
                    w->filename = strdup (iter->path);
                }
            }
        }
    }
    
    dl_shallow_free (to_update);
}
