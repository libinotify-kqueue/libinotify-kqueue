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
worker_remove_many (i_watch *iw);

static void
worker_update_flags (i_watch *iw, uint32_t flags);

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

    SLIST_INIT (&wrk->head);

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
 * Remove an inotify watch from worker.
 *
 * @param[in] wrk A pointer to #worker.
 * @param[in] iw A pointer to #i_watch to remove.
 **/
static void
worker_remove_iwatch (worker *wrk, i_watch *iw)
{
    assert (wrk != NULL);
    assert (iw != NULL);

    SLIST_REMOVE (&wrk->head, iw, i_watch, next);
    worker_remove_many (iw);
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
    i_watch *iw, *tmp;

    close (wrk->io[KQUEUE_FD]);
    wrk->io[KQUEUE_FD] = -1;

    close (wrk->kq);
    wrk->closed = 1;

    worker_cmd_release (&wrk->cmd);
    SLIST_FOREACH_SAFE (iw, &wrk->head, next, tmp) {
        worker_remove_iwatch (wrk, iw);
    }

    for (i = 0; i < wrk->iovcnt; i++) {
        free (wrk->iov[i].iov_base);
    }
    free (wrk->iov);
    pthread_mutex_destroy (&wrk->mutex);

    free (wrk);
}

/**
 * When starting watching a directory, start also watching its contents.
 * Initialize inotify watch.
 *
 * This function creates and initializes additional watches for a directory.
 *
 * @param[in] wrk    A pointer to #worker.
 * @param[in] path   Path to watch.
 * @param[in] flags  A combination of inotify event flags.
 * @return A pointer to a created #i_watch on success NULL otherwise
 **/
static i_watch *
worker_add_watch (worker     *wrk,
                  const char *path,
                  uint32_t    flags)
{
    assert (wrk != NULL);
    assert (path != NULL);

    int fd = watch_open (AT_FDCWD, path, flags);
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

    i_watch *iw = calloc (1, sizeof (i_watch));
    if (iw == NULL) {
        perror_msg ("Failed to allocate inotify watch");
        if (S_ISDIR (st.st_mode)) {
            dl_free (deps);
        }
        close (fd);
        return NULL;
    }
    iw->deps = deps;
    iw->wrk = wrk;
    iw->wd = fd;

    if (worker_sets_init (&iw->watches) == -1) {
        if (S_ISDIR (st.st_mode)) {
            dl_free (deps);
        }
        close (fd);
        free (iw);
        return NULL;
    }

    watch *parent = watch_init (WATCH_USER, iw->wrk->kq, path, fd, flags);
    if (parent == NULL) {
        worker_sets_free (&iw->watches);
        if (S_ISDIR (st.st_mode)) {
            dl_free (deps);
        }
        close (fd);
        free (iw);
        return NULL;
    }

    if (worker_sets_insert (&iw->watches, parent)) {
        if (S_ISDIR (st.st_mode)) {
            dl_free (deps);
        }
        watch_free (parent);
        free (iw);
        return NULL;
    }

    if (S_ISDIR (st.st_mode)) {

        dep_node *iter;
        SLIST_FOREACH (iter, &iw->deps->head, next) {
            watch *neww = worker_add_subwatch (iw, iter->item);
            if (neww == NULL) {
                perror_msg ("Failed to start watching a dependency %s of %s",
                            iter->item->path,
                            parent->filename);
            }
        }
    }
    return iw;
}

/**
 * Start watching a file or a directory.
 *
 * @param[in] iw         A pointer to #i_watch.
 * @param[in] di         Dependency item with relative path to watch.
 * @return A pointer to a created watch.
 **/
watch*
worker_add_subwatch (i_watch *iw, dep_item *di)
{
    assert (iw != NULL);
    assert (iw->deps != NULL);
    assert (di != NULL);

    int fd = watch_open (iw->wd, di->path, IN_DONT_FOLLOW);
    if (fd == -1) {
        perror_msg ("Failed to open file %s", di->path);
        return NULL;
    }

    watch *w = watch_init (WATCH_DEPENDENCY,
                           iw->wrk->kq,
                           di->path,
                           fd,
                           iw->watches.watches[0]->flags);
    if (w == NULL) {
        close (fd);
        return NULL;
    }

    if (worker_sets_insert (&iw->watches, w)) {
        watch_free (w);
        return NULL;
    }

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

    /* look up for an entry with this filename */
    i_watch *iw;
    SLIST_FOREACH (iw, &wrk->head, next) {

        size_t i = 0;
        for (i = 0; i < iw->watches.length; i++) {
            const char *evpath = iw->watches.watches[i]->filename;
            assert (evpath != NULL);

            if (iw->watches.watches[i]->type == WATCH_USER &&
                strcmp (path, evpath) == 0) {
                worker_update_flags (iw, flags);
                return iw->wd;
            }
        }
    }

    /* create a new entry if path is not found */
    iw = worker_add_watch (wrk, path, flags);
    if (iw == NULL) {
        return -1;
    }

    /* add inotify watch to worker`s watchlist */
    SLIST_INSERT_HEAD (&wrk->head, iw, next);

    return iw->wd;
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

    i_watch *iw;
    SLIST_FOREACH (iw, &wrk->head, next) {

        if (iw->wd == id) {
            enqueue_event (iw, IN_IGNORED, 0, NULL);
            flush_events (wrk);
            worker_remove_iwatch (wrk, iw);
            break;
        }
    }
    /* Assume always success */
    return 0;
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
static void
worker_update_flags (i_watch *iw, uint32_t flags)
{
    assert (iw != NULL);

    size_t i;
    for (i = 0; i < iw->watches.length; i++) {
        watch *w = iw->watches.watches[i];
        w->flags = flags;
        uint32_t fflags = inotify_to_kqueue (flags,
                                             w->is_really_dir,
                                             w->type != WATCH_USER);
        watch_register_event (w, iw->wrk->kq, fflags);
    }
}

/**
 * Remove an inotify watch.
 *
 * @param[in] iw      A pointer to #i_watch to remove.
 **/
static void
worker_remove_many (i_watch *iw)
{
    assert (iw != NULL);

    worker_sets_free (&iw->watches);
    if (iw->deps != NULL) {
        dl_free (iw->deps);
    }
    free (iw);
}

/**
 * Remove a watch from worker by its path.
 *
 * @param[in] iw      A pointer to the #i_watch.
 * @param[in] item    A dependency list item to remove watch.
 **/
void
worker_remove_watch (i_watch *iw, const dep_item *item)
{
    assert (iw != NULL);
    assert (item != NULL);

    size_t i;

    for (i = 0; i < iw->watches.length; i++) {
        watch *w = iw->watches.watches[i];

        if ((item->inode == w->inode)
          && (strcmp (item->path, w->filename) == 0)) {
            worker_sets_delete (&iw->watches, i);
            break;
        }
    }
}

/**
 * Update path of child watch for a specified watch.
 *
 * It is necessary when renames in the watched directory occur.
 *
 * @param[in] iw     A pointer to #i_watch.
 * @param[in] from   A rename from. Must be child of the specified parent.
 * @param[in] to     A rename to. Must be child of the specified parent.
 **/
void
worker_rename_watch (i_watch *iw, dep_item *from, dep_item *to)
{
    assert (iw != NULL);
    assert (from != NULL);
    assert (to != NULL);

    size_t i;

    for (i = 0; i < iw->watches.length; i++) {
        watch *w = iw->watches.watches[i];

        if ((from->inode == w->inode)
          && (strcmp (from->path, w->filename) == 0)) {
            free (w->filename);
            w->filename = strdup (to->path);
            break;
        }
    }
}
