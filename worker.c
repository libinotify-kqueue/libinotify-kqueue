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
#include <sys/socket.h>
#include <sys/stat.h>

#include <assert.h>
#include <dirent.h>
#include <fcntl.h> /* open() */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* close() */

#include "sys/inotify.h"

#include "compat.h"
#include "event-queue.h"
#include "inotify-watch.h"
#include "utils.h"
#include "watch.h"
#include "worker-thread.h"
#include "worker.h"

static void
worker_cmd_reset (struct worker_cmd *cmd);


/**
 * Prepare a command with the data of the inotify_add_watch() call.
 *
 * @param[in] cmd      A pointer to #worker_cmd.
 * @param[in] filename A file name of the watched entry.
 * @param[in] mask     A combination of the inotify watch flags.
 **/
void
worker_cmd_add (struct worker_cmd *cmd, const char *filename, uint32_t mask)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_ADD;
    cmd->cmd.add.filename = filename;
    cmd->cmd.add.mask = mask;
}


/**
 * Prepare a command with the data of the inotify_rm_watch() call.
 *
 * @param[in] cmd       A pointer to #worker_cmd
 * @param[in] watch_id  The identificator of a watch to remove.
 **/
void
worker_cmd_remove (struct worker_cmd *cmd, int watch_id)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_REMOVE;
    cmd->cmd.rm_id = watch_id;
}

/**
 * Prepare a command with the data of the libinotify_set_param() call.
 *
 * @param[in] cmd    A pointer to #worker_cmd
 * @param[in] param  Worker-thread parameter name to set.
 * @param[in] value  Worker-thread parameter value to set.
 **/
void
worker_cmd_param (struct worker_cmd *cmd, int param, intptr_t value)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_PARAM;
    cmd->cmd.param.param = param;
    cmd->cmd.param.value = value;
}

/**
 * Prepare a command that signals the worker shutdown.
 *
 * @param[in] cmd    A pointer to #worker_cmd
 **/
void
worker_cmd_close (struct worker_cmd *cmd)
{
    assert (cmd != NULL);
    worker_cmd_reset (cmd);

    cmd->type = WCMD_CLOSE;
}

/**
 * Reset the worker command.
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
static void
worker_cmd_reset (struct worker_cmd *cmd)
{
    assert (cmd != NULL);

    memset (cmd, 0, sizeof (struct worker_cmd));
}

/**
 * Signal user thread if worker command is done
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_post (struct worker *wrk)
{
    assert (wrk != NULL);

    worker_lock (wrk);
    ++wrk->sema;
    pthread_cond_broadcast (&wrk->cv);
    worker_unlock (wrk);

}

/**
 * Wait for worker command to complete
 *
 * @param[in] cmd A pointer to #worker_cmd.
 **/
void
worker_wait (struct worker *wrk)
{
    assert (wrk != NULL);

    worker_lock (wrk);
    while (wrk->sema == 0) {
        pthread_cond_wait (&wrk->cv, &wrk->mutex);
    }
    --wrk->sema;
    worker_unlock (wrk);
}

/**
 * Signal worker thread that #worker_cmd should be executed.
 *
 * @param[in] wrk A pointer to #worker.
 * @param[in] cmd A pointer to #worker_cmd passed to #worker.
 * @return positive number or 0 on success, -1 on error
 **/
int
worker_notify (struct worker *wrk, struct worker_cmd *cmd)
{
#ifdef EVFILT_USER
    struct kevent ke;

    EV_SET (&ke,
            wrk->io[KQUEUE_FD],
            EVFILT_USER,
            0,
            NOTE_TRIGGER,
#ifdef __DragonFly__
            /* DragonflyBSD does not copy udata */
            (intptr_t)cmd,
            0
#else
            0,
            cmd
#endif
            );
    return kevent (wrk->kq, &ke, 1, NULL, 0, zero_tsp);
#else
    return write (wrk->io[INOTIFY_FD], &cmd, sizeof (cmd));
#endif
}

/**
 * Set communication pipe buffer size
 * @param[in] wrk     A pointer to #worker.
 * @param[in] bufsize A buffer size allocated for communication pipe
 * @return 0 on success, -1 otherwise
 **/
int
worker_set_sockbufsize (struct worker *wrk, int bufsize)
{
    assert (wrk != NULL);

    if (bufsize <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (set_sndbuf_size (wrk->io[KQUEUE_FD], bufsize)) {
        perror_msg (("Failed to set send buffer size for socket"));
        return -1;
    }
#ifndef EVFILT_EMPTY
    {
        struct kevent ev;
        EV_SET (&ev,
                wrk->io[KQUEUE_FD],
                EVFILT_WRITE,
                EV_ADD | EV_ENABLE | EV_CLEAR,
                NOTE_LOWAT,
                bufsize,
                0);

        if (kevent (wrk->kq, &ev, 1, NULL, 0, zero_tsp) == -1) {
            int save_errno = errno;
            bufsize = wrk->sockbufsize;
            set_sndbuf_size (wrk->io[KQUEUE_FD], bufsize);
            errno = save_errno;
            perror_msg (("Failed to register kqueue event on socket"));
            return -1;
        }
    }
#endif
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
        perror_msg (("Failed to create a socket pair"));
        return -1;
    }

#ifdef SO_NOSIGPIPE
    {
        int on = 1;
        setsockopt (fildes[KQUEUE_FD],
                    SOL_SOCKET,
                    SO_NOSIGPIPE,
                    &on,
                    sizeof(on));
    }
#endif

    if (set_nonblock_flag (fildes[KQUEUE_FD], 1) == -1) {
        perror_msg (("Failed to set socket into nonblocking mode"));
        return -1;
    }

    if (set_cloexec_flag (fildes[KQUEUE_FD], 1) == -1) {
        perror_msg (("Failed to set cloexec flag on socket"));
        return -1;
    }

    /* Check flags for both linux and BSD CLOEXEC values */
    if (set_cloexec_flag (fildes[INOTIFY_FD],
#ifdef O_CLOEXEC
                          flags & (IN_CLOEXEC|O_CLOEXEC)) == -1) {
#else
                          flags & IN_CLOEXEC) == -1) {
#endif
        perror_msg (("Failed to set cloexec flag on socket"));
        return -1;
    }

    /* Check flags for both linux and BSD NONBLOCK values */
    if (set_nonblock_flag (fildes[INOTIFY_FD],
                           flags & (IN_NONBLOCK|O_NONBLOCK)) == -1) {
        perror_msg (("Failed to set socket into nonblocking mode"));
        return -1;
    }

    return 0;
}

/**
 * Create a new worker and start its thread.
 *
 * @return A pointer to a new worker.
 **/
struct worker*
worker_create (int flags)
{
    pthread_attr_t attr;
    struct kevent ev[3];
    sigset_t set, oset;
    int result, nevents = 1;
    bool direct = flags & IN_DIRECT;

    struct worker* wrk = calloc (1, sizeof (struct worker));

    if (wrk == NULL) {
        perror_msg (("Failed to create a new worker"));
        goto failure;
    }

    wrk->io[INOTIFY_FD] = -1;
    wrk->io[KQUEUE_FD] = -1;

    wrk->kq = kqueue_init ();
    if (wrk->kq == -1) {
        perror_msg (("Failed to create a new kqueue"));
        goto failure;
    }

    if (direct) {
#ifndef EVFILT_USER
        perror_msg (("Direct mode requires support for EVFILT_USER"));
        goto failure;
#endif
        /* When operating in the direct mode the KQUEUE_FD is really a kqueue
         * descriptor, not an end of a socket pipe.
         */
        wrk->io[KQUEUE_FD] = kqueue_init ();
        if (wrk->io[KQUEUE_FD] == -1) {
            perror_msg (("Failed to create a new kqueue"));
            goto failure;
        }
        /* In direct mode we don't need any more descriptors, so we put the
         * same fd into INOTIFY_FD. This also allows other parts of the library
         * to figure in what mode we're running. */
        wrk->io[INOTIFY_FD] = wrk->io[KQUEUE_FD];
    } else {
        if (pipe_init (wrk->io, flags) == -1) {
            perror_msg (("Failed to create a pipe"));
            goto failure;
        }
        /* Set socket buffer size to IN_DEF_SOCKBUFSIZE bytes */
        if (worker_set_sockbufsize(wrk, IN_DEF_SOCKBUFSIZE) == -1) {
            goto failure;
        }
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
#ifdef EVFILT_EMPTY
    if (!direct) {
        /*
        * Modern FreeBSDs always report full sendbuffer size in data field of
        * EVFILT_WRITE kevent so we can not determine amount of data remaining in
        * it reliably. As we want to know exact amount of bytes to avoid partial
        * inotify event reads as much as possible, start using of EVFILT_EMPTY
        * to check available send buffer space. Note that we still use
        * EVFILT_WRITE with NOTE_LOWAT set too high to check EOF conditions.
        */
        EV_SET (&ev[1],
                wrk->io[KQUEUE_FD],
                EVFILT_WRITE,
                EV_ADD | EV_ENABLE | EV_CLEAR,
                NOTE_LOWAT,
                INT_MAX,
                0);
        EV_SET (&ev[2], wrk->io[KQUEUE_FD], EVFILT_EMPTY, EV_ADD | EV_CLEAR, 0, 0, 0);
        nevents = 3;
    }
#endif

    if (kevent (wrk->kq, ev, nevents, NULL, 0, zero_tsp) == -1) {
        perror_msg (("Failed to register kqueue event on pipe"));
        goto failure;
    }

    wrk->wd_last = 0;
    wrk->wd_overflow = false;

    pthread_mutex_init (&wrk->cmd_mtx, NULL);
    atomic_init (&wrk->mutex_rc, 0);
    pthread_mutex_init (&wrk->mutex, NULL);
    pthread_cond_init (&wrk->cv, NULL);
    wrk->sema = 0;
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
        perror_msg (("Failed to start a new worker thread"));
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
worker_free (struct worker *wrk)
{
    struct i_watch *iw;
    bool direct = wrk->io[KQUEUE_FD] == wrk->io[INOTIFY_FD];

    assert (wrk != NULL);

    if (wrk->io[KQUEUE_FD] != -1) {
        close (wrk->io[KQUEUE_FD]);
        wrk->io[KQUEUE_FD] = -1;
        if (direct)
            wrk->io[INOTIFY_FD] = -1;
    }

    close (wrk->kq);

#ifdef WORKER_FAST_WATCHSET_DESTROY
   watch_set_free (&wrk->watches);
#endif
    while (!SLIST_EMPTY (&wrk->head)) {
        iw = SLIST_FIRST (&wrk->head);
        SLIST_REMOVE_HEAD (&wrk->head, next);
        iwatch_free (iw);
    }

    /* Wait for user thread(s) to release worker`s mutex */
    while (atomic_load (&wrk->mutex_rc) > 0) {
        worker_cmd_lock (wrk);
        worker_cmd_unlock (wrk);
    }
    pthread_mutex_destroy (&wrk->cmd_mtx);
    /* And only after that destroy worker_cmd sync primitives */
    pthread_cond_destroy (&wrk->cv);
    pthread_mutex_destroy (&wrk->mutex);
    event_queue_free (&wrk->eq);
    free (wrk);
}

/**
 * Allocate new inotify watch descriptor.
 *
 * @param[in] wrk   A pointer to #worker.
 * @return An unique (per watch) newly allocated descriptor
 **/
int
worker_allocate_wd (struct worker *wrk)
{
    bool allocated;

    do {
        if (wrk->wd_last == INT_MAX) {
            wrk->wd_last = 0;
            wrk->wd_overflow = true;
        }
        allocated = true;
        ++wrk->wd_last;
        if (wrk->wd_overflow) {
            struct i_watch *iw;
            SLIST_FOREACH (iw, &wrk->head, next) {
                if (iw->wd == wrk->wd_last) {
                    allocated = false;
                    break;
                }
            }
        }
    } while (!allocated);

    return wrk->wd_last;
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
worker_add_or_modify (struct worker *wrk,
                      const char    *path,
                      uint32_t       flags)
{
    int fd;
    struct stat st;
    struct watch *w;
    struct i_watch *iw;

    assert (path != NULL);
    assert (wrk != NULL);

    /* Open inotify watch descriptor */
    fd = iwatch_open (path, flags);
    if (fd == -1) {
        return -1;
    }

    if (fstat (fd, &st) == -1) {
        perror_msg (("Failed to stat file %s", path));
        close (fd);
        return -1;
    }

    /* look up for an entry with these inode&device numbers */
    w = watch_set_find (&wrk->watches, st.st_dev, st.st_ino);
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
    iw = iwatch_init (wrk, fd, flags);
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
worker_remove (struct worker *wrk, int id)
{
    struct i_watch *iw;

    assert (wrk != NULL);
    assert (id >= 0);

    SLIST_FOREACH (iw, &wrk->head, next) {

        if (iw->wd == id) {
            worker_remove_iwatch (wrk, iw);
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

/**
 * Stop and remove a watch.
 *
 * @param[in] wrk A pointer to #worker.
 * @param[in] iw  A pointer to #i_watch to remove.
 **/
void
worker_remove_iwatch (struct worker *wrk, struct i_watch *iw)
{
    assert (wrk != NULL);
    assert (iw != NULL);

    event_queue_enqueue (&wrk->eq, iw->wd, IN_IGNORED, 0, NULL);
    SLIST_REMOVE (&wrk->head, iw, i_watch, next);
    iwatch_free (iw);
}

/**
 * Prepare a command with the data of the libinotify_set_param() call.
 *
 * @param[in] wrk   A pointer to #worker.
 * @param[in] param Worker-thread parameter name to set.
 * @param[in] value Worker-thread parameter value to set.
 * @return 0 on success, -1 on failure.
 **/
int
worker_set_param (struct worker *wrk, int param, intptr_t value)
{
    assert (wrk != NULL);

    switch (param) {
    case IN_SOCKBUFSIZE:
        if(wrk->io[KQUEUE_FD] != wrk->io[INOTIFY_FD]) /* we have no sockets in direct mode */
            return worker_set_sockbufsize (wrk, value);
        else
            return 0;
    case IN_MAX_QUEUED_EVENTS:
        return event_queue_set_max_events (&wrk->eq, value);
    default:
        errno = EINVAL;
    }
    return -1;
}
