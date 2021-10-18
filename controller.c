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

#include <stddef.h> /* NULL */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>

#include "sys/inotify.h"

#include "utils.h"
#include "worker.h"


#define WORKER_SZ 100
static worker* volatile workers[WORKER_SZ];
static pthread_rwlock_t workers_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static bool initialized = false;
static worker dummy_wrk = {
    .io = { -1, -1 },
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

#define WRK_FREE (&dummy_wrk)
#define WRK_RESV NULL

static inline void
workerset_rlock (void)
{
    pthread_rwlock_rdlock (&workers_rwlock);
}

static inline void
workerset_wlock (void)
{
    pthread_rwlock_wrlock (&workers_rwlock);
}

static inline void
workerset_unlock (void)
{
    pthread_rwlock_unlock (&workers_rwlock);
}

static int     worker_exec (int fd, worker_cmd *cmd);
static void    workers_init (void);

/**
 * Create a new inotify instance.
 *
 * This function will create a new inotify instance (actually, a worker
 * with its own thread). To destroy the instance, just close its file
 * descriptor.
 *
 * @return  -1 on failure, a file descriptor on success.
 **/
int
inotify_init (void) __THROW
{
    return inotify_init1 (0);
}

/**
 * Create a new inotify instance.
 *
 * This function will create a new inotify instance (actually, a worker
 * with its own thread). To destroy the instance, just close its file
 * descriptor.
 *
 * @param[in] flags A combination of inotify_init1 flags
 * @return  -1 on failure, a file descriptor on success.
 **/
int
inotify_init1 (int flags) __THROW
{
    int lfd = -1;

#ifdef O_CLOEXEC
    if (flags & ~(IN_CLOEXEC|O_CLOEXEC|IN_NONBLOCK|O_NONBLOCK)) {
#else
    if (flags & ~(IN_CLOEXEC|IN_NONBLOCK|O_NONBLOCK)) {
#endif
        errno = EINVAL;
        return -1;
    }

    workerset_wlock ();

    if (!initialized) {
        workers_init();
    }

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i] == WRK_FREE) {
            workers[i] = WRK_RESV;
            break;
        }
    }

    workerset_unlock ();

    if (i == WORKER_SZ) {
        errno = EMFILE;
        return -1;
    }

    worker *wrk = worker_create (flags);
    if (wrk == NULL) {
        workers[i] = WRK_FREE;
        return -1;
    }

    workers[i] = wrk;
    lfd = wrk->io[INOTIFY_FD];

    /* We can face into situation when there are two workers with the same
     * inotify FDs. It usually occurs when a worker fd has been closed but
     * the worker has not been removed from a list yet. The fd is free, and
     * when we create a new worker, we can * receive the same fd. So check
     * for duplicates and remove them now. */
    int j;
    for (j = 0; j < WORKER_SZ; j++) {
        worker *jw = workers[j];
        if (jw != WRK_FREE && jw != WRK_RESV && jw->io[INOTIFY_FD] == lfd &&
            jw != wrk) {
            workers[j] = WRK_FREE;
            perror_msg ("Collision found: fd %d", lfd);
        }
    }

    return lfd;
}


/**
 * Add or modify a watch.
 *
 * If the watch with a such filename is already exist, its mask will
 * be updated. A new watch will be created otherwise.
 *
 * @param[in] fd   A file descriptor of an inotify instance.
 * @param[in] name A path to a file to watch.
 * @param[in] mask A combination of inotify flags. 
 * @return id of a watch, -1 on failure.
 **/
int
inotify_add_watch (int         fd,
                   const char *name,
                   uint32_t    mask) __THROW
{
    struct stat st;
    worker_cmd cmd;

    if (!is_opened (fd)) {
        return -1;	/* errno = EBADF */
    }

    /*
     * this lstat() call guards worker from incorrectly specified path.
     * E.g, it prevents catching of SIGSEGV when pathname points outside
     * of the process's accessible address space
     */
    if (lstat (name, &st) == -1) {
        perror_msg("failed to lstat watch %s",
                   errno != EFAULT ? name : "<bad addr>");
        return -1;
    }

    if (mask == 0) {
        perror_msg ("Failed to open watch %s. Bad event mask %x", name, mask);
        errno = EINVAL;
        return -1;
    }

    worker_cmd_add (&cmd, name, mask);
    return worker_exec (fd, &cmd);
}

/**
 * Remove a watch.
 *
 * Removes a watch and releases all the associated resources.
 * Notifications from the watch should not be received by a client
 * anymore.
 *
 * @param[in] fd Inotify instance file descriptor.
 * @param[in] wd Watch id.
 * @return 0 on success, -1 on failure.
 **/
int
inotify_rm_watch (int fd,
                  int wd) __THROW
{
    worker_cmd cmd;

    if (wd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!is_opened (fd)) {
        return -1;	/* errno = EBADF */
    }

    worker_cmd_remove (&cmd, wd);
    return worker_exec (fd, &cmd);
}

/**
 * Prepare a command with the data of the inotify_set_param() call.
 *
 * @param[in] fd    Inotify instance file descriptor.
 * @param[in] param Worker-thread parameter name to set.
 * @param[in] value Worker-thread parameter value to set.
 * @return 0 on success, -1 on failure.
 **/
int
inotify_set_param (int fd, int param, intptr_t value)
{
    worker_cmd cmd;

    if (!is_opened (fd)) {
        return -1;	/* errno = EBADF */
    }

    worker_cmd_param (&cmd, param, value);
    return worker_exec (fd, &cmd);
}

/**
 * Erase a worker from a list of workers.
 * 
 * This function does not lock the global array of workers (I assume that
 * marking its items as volatile should be enough). Also this function is
 * intended to be called from the worker threads only.
 * 
 * @param[in] wrk A pointer to a worker
 **/
void
worker_erase (worker *wrk)
{
    assert (wrk != NULL);

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i] == wrk) {
            workers[i] = WRK_FREE;
            break;
        }
    }
}

/**
 * Execute command in context of working thread.
 *
 * @param[in] fd  Inotify instance file descriptor.
 * @param[in] cmd Pointer to #worker_cmd
 * @return 0 on success, -1 on failure with errno set.
 **/
static int
worker_exec (int fd, worker_cmd *cmd)
{
    if (!initialized) {
        errno = EINVAL;
        return -1;
    }

    workerset_rlock ();

    /* look up for an appropriate worker */
    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        worker *wrk = workers[i];
        if (wrk != WRK_FREE && wrk != WRK_RESV && wrk->io[INOTIFY_FD] == fd) {
            worker_lock (wrk);
            if (wrk != workers[i]) {
                /* RACE: worker thread overwrote worker pointer in between
                   obtaining pointer on wrk and locking its mutex. */
                perror_msg ("race detected. fd: %d", fd);
                worker_unlock (wrk);
                workerset_unlock ();
                errno = EBADF;
                return -1;
            }

            cmd->retval = -1;
            cmd->error = EBADF;
#ifdef EVFILT_USER
            struct kevent ke;
            /* Pass cmd in data field as DragonflyBSD does not copy udata */
            EV_SET (&ke,
                    wrk->io[KQUEUE_FD],
                    EVFILT_USER,
                    0,
                    NOTE_TRIGGER,
                    (intptr_t)cmd,
                    0);
            if (kevent (wrk->kq, &ke, 1, NULL, 0, NULL) != -1) {
#else
            if (safe_write (wrk->io[INOTIFY_FD], &cmd, sizeof (cmd)) != -1) {
#endif
                worker_wait (wrk);
            }

            worker_unlock (wrk);
            workerset_unlock ();
            if (cmd->retval == -1) {
                errno = cmd->error;
            }
            return cmd->retval;
        }
    }

    workerset_unlock ();
    errno = EINVAL;
    return -1;
}

/**
 * Initialize inotify at first first use. Should be run via pthread_once
 **/
static void
workers_init (void)
{
    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        workers[i] = WRK_FREE;
    }
    initialized = true;
}
