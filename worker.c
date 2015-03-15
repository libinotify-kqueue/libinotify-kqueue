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
#include <sys/stat.h>

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
 * Initialize resources associated with worker command.
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void worker_cmd_init (worker_cmd *cmd)
{
    assert (cmd != NULL);
    memset (cmd, 0, sizeof (worker_cmd));
    pthread_barrier_init (&cmd->sync, NULL, 2);
}

/**
 * Prepare a command with the data of the inotify_add_watch() call.
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
}


/**
 * Prepare a command with the data of the inotify_rm_watch() call.
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
    cmd->type = 0;
    cmd->retval = 0;
    cmd->add.filename = NULL;
    cmd->add.mask = 0;
    cmd->rm_id = 0;
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
    pthread_barrier_wait (&cmd->sync);
}

/**
 * Release a worker command.
 *
 * This function releases resources associated with worker command.
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_cmd_release (worker_cmd *cmd)
{
    assert (cmd != NULL);
    pthread_barrier_destroy (&cmd->sync);
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
    struct kevent ev;

    worker* wrk = calloc (1, sizeof (worker));

    if (wrk == NULL) {
        perror_msg ("Failed to create a new worker");
        goto failure;
    }

    wrk->iovalloc = 0;
    wrk->iovcnt = 0;
    wrk->iov = NULL;

    wrk->kq = kqueue ();
    if (wrk->kq == -1) {
        perror_msg ("Failed to create a new kqueue");
        goto failure;
    }

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, (int *) wrk->io) == -1) {
        perror_msg ("Failed to create a socket pair");
        goto failure;
    }

    if (worker_sets_init (&wrk->sets) == -1) {
        goto failure;
    }

    EV_SET (&ev,
            wrk->io[KQUEUE_FD],
            EVFILT_READ,
            EV_ADD | EV_ENABLE | EV_CLEAR,
            NOTE_LOWAT,
            1,
            0);

    if (kevent (wrk->kq, &ev, 1, NULL, 0, NULL) == -1) {
        perror_msg ("Failed to register kqueue event on pipe");
        goto failure;
    }

    pthread_mutex_init (&wrk->mutex, NULL);

    worker_cmd_init (&wrk->cmd);

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

    int i;

    close (wrk->io[KQUEUE_FD]);
    wrk->io[KQUEUE_FD] = -1;

    close (wrk->kq);
    wrk->closed = 1;

    worker_cmd_release (&wrk->cmd);
    worker_sets_free (&wrk->sets);

    for (i = 0; i < wrk->iovcnt; i++) {
        free (wrk->iov[i].iov_base);
    }
    free (wrk->iov);
    pthread_mutex_destroy (&wrk->mutex);

    free (wrk);
}

/**
 * Start watching a file or a directory.
 *
 * @param[in] wrk        A pointer to #worker.
 * @param[in] path       A file name of a watched file.
 * @param[in] fd         A file descriptor of a watched entry.
 * @param[in] flags      A combination of inotify event flags.
 * @param[in] type       The type of a watch.
 * @return A pointer to a created watch.
 **/
static watch*
worker_start_watching (worker      *wrk,
                       const char  *path,
                       int          fd,
                       uint32_t     flags,
                       watch_type_t type)
{
    assert (wrk != NULL);
    assert (path != NULL);

    if (worker_sets_extend (&wrk->sets, 1) == -1) {
        perror_msg ("Failed to extend worker sets");
        return NULL;
    }

    watch *w = watch_init (type, wrk->kq, path, fd, flags);
    if (w == NULL) {
        return NULL;
    }
    wrk->sets.watches[wrk->sets.length] = w;

    ++wrk->sets.length;

    return w;
}

/**
 * When starting watching a directory, start also watching its contents.
 *
 * This function creates and initializes additional watches for a directory.
 *
 * @param[in] wrk    A pointer to #worker.
 * @param[in] path   Path to watch.
 * @param[in] flags  A combination of inotify event flags.
 * @return A pointer to a created watch on success NULL otherwise
 **/
static watch *
worker_add_watch (worker     *wrk,
                  const char *path,
                  uint32_t    flags)
{
    assert (wrk != NULL);
    assert (path != NULL);

    int fd = watch_open (AT_FDCWD, path);
    if (fd == -1) {
        perror_msg ("Failed to open watch %s", path);
        return NULL;
    }

    struct stat st;
    if (fstat (fd, &st) == -1) {
        perror_msg ("Failed to fstat watch %s on init", path);
        close (fd);
        return NULL;
    }

    dep_list *deps = NULL;
    if (S_ISDIR (st.st_mode)) {
        deps = dl_listing (fd);
        if (deps == NULL) {
            perror_msg ("Directory listing of %s failed", path);
            close (fd);
            return NULL;
        }
    }

    watch *parent = worker_start_watching (wrk, path, fd, flags, WATCH_USER);
    if (parent == NULL) {
        if (S_ISDIR (st.st_mode)) {
            dl_free (deps);
        }
        close (fd);
        return NULL;
    }

    if (S_ISDIR (st.st_mode)) {
        parent->deps = deps;

        dep_node *iter;
        SLIST_FOREACH (iter, &parent->deps->head, next) {
            watch *neww = worker_add_subwatch (wrk, parent, iter->item);
            if (neww == NULL) {
                perror_msg ("Failed to start watching a dependency %s of %s",
                            iter->item->path,
                            parent->filename);
            }
        }
    }
    return parent;
}

/**
 * Start watching a file or a directory.
 *
 * @param[in] wrk        A pointer to #worker.
 * @param[in] parent     A pointer to the parent #watch, i.e. the watch we add
 *     dependencies for.
 * @param[in] di         Dependency item with relative path to watch.
 * @return A pointer to a created watch.
 **/
watch*
worker_add_subwatch (worker *wrk, watch *parent, dep_item *di)
{
    assert (wrk != NULL);
    assert (parent != NULL);
    assert (di != NULL);

    int fd = watch_open (parent->fd, di->path);
    if (fd == -1) {
        perror_msg ("Failed to open file %s", di->path);
        return NULL;
    }

    watch *w = worker_start_watching (wrk,
                                      di->path,
                                      fd,
                                      parent->flags,
                                      WATCH_DEPENDENCY);
    if (w == NULL) {
        close (fd);
        return NULL;
    }

    w->parent = parent;

    return w;
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
    assert (sets->watches != NULL);

    /* look up for an entry with this filename */
    size_t i = 0;
    for (i = 0; i < sets->length; i++) {
        const char *evpath = sets->watches[i]->filename;
        assert (evpath != NULL);

        if (sets->watches[i]->type == WATCH_USER &&
            strcmp (path, evpath) == 0) {
            worker_update_flags (wrk, sets->watches[i], flags);
            return sets->watches[i]->fd;
        }
    }

    /* add a new entry if path is not found */
    watch *w = worker_add_watch (wrk, path, flags);
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
    for (i = 0; i < wrk->sets.length; i++) {
        if (wrk->sets.watches[i]->fd == id) {
            worker_remove_many (wrk,
                                wrk->sets.watches[i],
                                wrk->sets.watches[i]->deps,
                                1);

            enqueue_event (wrk, id, IN_IGNORED, 0, NULL);
            flush_events (wrk);
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

    w->flags = flags;
    uint32_t fflags = inotify_to_kqueue (flags, w->is_really_dir, 0);
    watch_register_event (w, wrk->kq, fflags);

    /* Propagate the flag changes also on all dependent watches */
    if (w->deps) {

        /* Yes, it is quite stupid to iterate over ALL watches of a worker
         * while we have a linked list of its dependencies.
         * TODO improve it */
        size_t i;
        for (i = 0; i < wrk->sets.length; i++) {
            watch *depw = wrk->sets.watches[i];
            if (depw->parent == w) {
                depw->flags = flags;
                uint32_t fflags = inotify_to_kqueue (flags,
                                                     depw->is_really_dir,
                                                     1);
                watch_register_event (depw, wrk->kq, fflags);
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

    dep_node *iter;
    size_t i;

    if (items != NULL) {
        SLIST_FOREACH (iter, &items->head, next) {

            worker_remove_watch (wrk, parent, iter->item);
        }
    }

    if (remove_self) {
        for (i = 0; i < wrk->sets.length; i++) {
            if (wrk->sets.watches[i] == parent) {
                worker_sets_delete (&wrk->sets, i);
                break;
            }
        }
    }
}

/**
 * Remove a watch from worker by its path.
 *
 * @param[in] wrk     A pointer to #worker.
 * @param[in] parent  A pointer to the parent #watch.
 * @param[in] item    A watch to remove. Must be child of the specified parent.
 **/
void
worker_remove_watch (worker *wrk, watch *parent, const dep_item *item)
{
    assert (wrk != NULL);
    assert (parent != NULL);

    size_t i;

    for (i = 0; i < wrk->sets.length; i++) {
        watch *w = wrk->sets.watches[i];

        if ((w->parent == parent) && (strcmp (item->path, w->filename) == 0)) {
            worker_sets_delete (&wrk->sets, i);
            break;
        }
    }
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
    size_t i;

    for (i = 0; i < wrk->sets.length; i++) {
        dep_node *iter;
        dep_node *prev = NULL;
        watch *w = wrk->sets.watches[i];

        if (w->parent == parent) {
            SLIST_FOREACH (iter, &to_update->head, next) {
                if (iter->item->inode == w->inode) {
                    break;
                }
                prev = iter;
            }

            if (iter != NULL) {
                if (strcmp (iter->item->path, w->filename)) {
                    free (w->filename);
                    w->filename = strdup (iter->item->path);
                }

                dl_remove_after (to_update, prev);
            }
        }
    }
    
    dl_shallow_free (to_update);
}
