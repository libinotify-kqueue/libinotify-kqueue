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

#include <pthread.h>
#include <signal.h> 
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

#include "event-queue.h"
#include "utils.h"
#include "watch.h"
#include "worker-thread.h"
#include "worker.h"

static void
worker_cmd_reset (worker_cmd *cmd);


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
    cmd->add.filename = filename;
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
 * Prepare a command with the data of the inotify_set_param() call.
 *
 * @param[in] cmd    A pointer to #worker_cmd
 * @param[in] param  Worker-thread parameter name to set.
 * @param[in] value  Worker-thread parameter value to set.
 **/
void
worker_cmd_param (worker_cmd *cmd, int param, intptr_t value)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_PARAM;
    cmd->param.param = param;
    cmd->param.value = value;
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

    cmd->type = 0;
    cmd->retval = 0;
    cmd->add.filename = NULL;
    cmd->add.mask = 0;
    cmd->rm_id = 0;
    cmd->param.param = 0;
    cmd->param.value = 0;
}

/**
 * Signal user thread if worker command is done
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_post (worker *wrk)
{
    assert (wrk != NULL);
    ik_sem_post(&wrk->sync_sem);
}

/**
 * Wait for worker command to complete
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_wait (worker *wrk)
{
    assert (wrk != NULL);
    ik_sem_wait(&wrk->sync_sem);
}

/**
 * Set communication pipe buffer size
 * @param[in] wrk     A pointer to #worker.
 * @param[in] bufsize A buffer size allocated for communication pipe
 * @return 0 on success, -1 otherwise
 **/
int
worker_set_sockbufsize (worker *wrk, int bufsize)
{
    assert (wrk != NULL);

    if (bufsize <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (setsockopt(wrk->io[KQUEUE_FD],
                   SOL_SOCKET,
                   SO_SNDBUF,
                   &bufsize,
                   sizeof(bufsize))) {
        perror_msg ("Failed to set send buffer size for socket");
        return -1;
    }
    wrk->sockbufsize = bufsize;

    return 0;
}

/**
 * Create communication pipe
 *
 * @param[in] filedes A pair of descriptors used in referencing the new pipe
 * @param[in] flags   A Pipe flags in inotify(linux) or fcntl.h(BSD) format
 * @return 0 on success, -1 otherwise
 **/
static int
pipe_init (int fildes[2], int flags)
{
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, fildes) == -1) {
        perror_msg ("Failed to create a socket pair");
        return -1;
    }

#ifdef SO_NOSIGPIPE
     int on = 1;
     setsockopt (fildes[KQUEUE_FD], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    if (set_cloexec_flag (fildes[KQUEUE_FD], 1) == -1) {
        perror_msg ("Failed to set cloexec flag on socket");
        return -1;
    }

    /* Check flags for both linux and BSD CLOEXEC values */
    if (set_cloexec_flag (fildes[INOTIFY_FD],
#ifdef O_CLOEXEC
                          flags & (IN_CLOEXEC|O_CLOEXEC)) == -1) {
#else
                          flags & IN_CLOEXEC) == -1) {
#endif
        perror_msg ("Failed to set cloexec flag on socket");
        return -1;
    }

    /* Check flags for both linux and BSD NONBLOCK values */
    if (set_nonblock_flag (fildes[INOTIFY_FD],
                           flags & (IN_NONBLOCK|O_NONBLOCK)) == -1) {
        perror_msg ("Failed to set socket into nonblocking mode");
        return -1;
    }

    return 0;
}

/**
 * Create a new worker and start its thread.
 *
 * @return A pointer to a new worker.
 **/
worker*
worker_create (int flags)
{
    pthread_attr_t attr;
    struct kevent ev[2];
    sigset_t set, oset;
    int result;

    worker* wrk = calloc (1, sizeof (worker));

    if (wrk == NULL) {
        perror_msg ("Failed to create a new worker");
        goto failure;
    }

    wrk->io[INOTIFY_FD] = -1;
    wrk->io[KQUEUE_FD] = -1;

    wrk->kq = kqueue ();
    if (wrk->kq == -1) {
        perror_msg ("Failed to create a new kqueue");
        goto failure;
    }

    if (pipe_init ((int *) wrk->io, flags) == -1) {
        perror_msg ("Failed to create a pipe");
        goto failure;
    }

    /* Set socket buffer size to IN_DEF_SOCKBUFSIZE bytes */
    if (worker_set_sockbufsize(wrk, IN_DEF_SOCKBUFSIZE) == -1) {
        goto failure;
    }

    SLIST_INIT (&wrk->head);

#ifdef EVFILT_USER
    EV_SET (&ev[0], wrk->io[KQUEUE_FD], EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, 0);
#else
    EV_SET (&ev[0],
            wrk->io[KQUEUE_FD],
            EVFILT_READ,
            EV_ADD | EV_ENABLE | EV_CLEAR,
            NOTE_LOWAT,
            1,
            0);
#endif

    EV_SET (&ev[1],
            wrk->io[KQUEUE_FD],
            EVFILT_WRITE,
            EV_ADD | EV_ENABLE | EV_CLEAR,
            0,
            0,
            0);

    if (kevent (wrk->kq, ev, 2, NULL, 0, NULL) == -1) {
        perror_msg ("Failed to register kqueue event on pipe");
        goto failure;
    }

    wrk->wd_last = 0;
    wrk->wd_overflow = 0;

    pthread_mutex_init (&wrk->mutex, NULL);
    atomic_init (&wrk->mutex_rc, 0);
    ik_sem_init (&wrk->sync_sem, 0, 0);
    event_queue_init (&wrk->eq);
    watch_set_init (&wrk->watches);

    /* create a run a worker thread */
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

    sigfillset (&set);
    pthread_sigmask (SIG_BLOCK, &set, &oset);

    result = pthread_create (&wrk->thread, &attr, worker_thread, wrk);
    
    pthread_attr_destroy (&attr);
    pthread_sigmask (SIG_SETMASK, &oset, NULL);

    if (result != 0) {
        perror_msg ("Failed to start a new worker thread");
        goto failure;
    }

    return wrk;
    
failure:
    if (wrk != NULL) {
        if (wrk->io[INOTIFY_FD] != -1) {
            close (wrk->io[INOTIFY_FD]);
        }
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

    i_watch *iw;

    if (wrk->io[KQUEUE_FD] != -1) {
        close (wrk->io[KQUEUE_FD]);
        wrk->io[KQUEUE_FD] = -1;
    }

    close (wrk->kq);

    while (!SLIST_EMPTY (&wrk->head)) {
        iw = SLIST_FIRST (&wrk->head);
        SLIST_REMOVE_HEAD (&wrk->head, next);
        iwatch_free (iw);
    }

    /* Wait for user thread(s) to release worker`s mutex */
    while (atomic_load (&wrk->mutex_rc) > 0) {
        worker_lock (wrk);
        worker_unlock (wrk);
    }
    pthread_mutex_destroy (&wrk->mutex);
    /* And only after that destroy worker_cmd sync primitives */
    ik_sem_destroy (&wrk->sync_sem);
    event_queue_free (&wrk->eq);
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

    struct stat st;
    if (fstat (fd, &st) == -1) {
        perror_msg ("Failed to stat file %s", path);
        close (fd);
        return -1;
    }

    /* look up for an entry with these inode&device numbers */
    struct watch *w = watch_set_find (&wrk->watches, st.st_dev, st.st_ino);
    if (w != NULL) {
        struct watch_dep *wd;
        close (fd);
        fd = w->fd;
        WD_FOREACH (wd, w) {
            if (watch_dep_is_parent (wd)) {
                iwatch_update_flags (wd->iw, flags);
                return wd->iw->wd;
            }
        }
    }

    /* create a new entry if watch is not found */
    i_watch *iw = iwatch_init (wrk, fd, flags);
    if (iw == NULL) {
        return -1;
    }

    /* Allocate watch descriptor */
    int allocated;
    do {
        if (wrk->wd_last == INT_MAX) {
            wrk->wd_last = 0;
            wrk->wd_overflow = 1;
        }
        allocated = 1;
        ++wrk->wd_last;
        if (wrk->wd_overflow) {
            SLIST_FOREACH (iw, &wrk->head, next) {
                if (iw->wd == wrk->wd_last) {
                    allocated = 0;
                    break;
                }
            }
        }
    } while (!allocated);
    iw->wd = wrk->wd_last;

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
            event_queue_enqueue (&wrk->eq, id, IN_IGNORED, 0, NULL);
            SLIST_REMOVE (&wrk->head, iw, i_watch, next);
            iwatch_free (iw);
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

/**
 * Prepare a command with the data of the inotify_set_param() call.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] param Worker-thread parameter name to set.
 * @param[in] value Worker-thread parameter value to set.
 * @return 0 on success, -1 on failure.
 **/
int
worker_set_param (worker *wrk, int param, intptr_t value)
{
    assert (wrk != NULL);

    switch (param) {
    case IN_SOCKBUFSIZE:
        return worker_set_sockbufsize (wrk, value);
    case IN_MAX_QUEUED_EVENTS:
        return event_queue_set_max_events (&wrk->eq, value);
    default:
        errno = EINVAL;
    }
    return -1;
}
