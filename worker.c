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
    iwatch_free (iw);
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

    /* Open inotify watch descriptor */
    int fd = iwatch_open (path, flags);
    if (fd == -1) {
        return -1;
    }

    /* look up for an entry with this filename */
    i_watch *iw;
    SLIST_FOREACH (iw, &wrk->head, next) {

        size_t i = 0;
        for (i = 0; i < iw->watches.length; i++) {
            const char *evpath = iw->watches.watches[i]->filename;
            assert (evpath != NULL);

            if (!(iw->watches.watches[i]->flags & WF_ISSUBWATCH) &&
                strcmp (path, evpath) == 0) {
                close (fd);
                iwatch_update_flags (iw, flags);
                return iw->wd;
            }
        }
    }

    /* create a new entry if path is not found */
    iw = iwatch_init (wrk, path, fd, flags);
    if (iw == NULL) {
        close (fd);
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
