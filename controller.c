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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/event.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h> /* NULL */
#include <stdlib.h>
#include <unistd.h>

#include "sys/inotify.h"

#include "compat.h"
#include "utils.h"
#include "worker.h"


static struct workers_list workers = SLIST_HEAD_INITIALIZER (&workers);
static atomic_uint nworkers;
static pthread_rwlock_t workers_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static unsigned int max_workers = IN_DEF_MAX_USER_INSTANCES;

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

static int     worker_exec (int fd, struct worker_cmd *cmd);

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
inotify_init (void)
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
inotify_init1 (int flags)
{
    struct worker *wrk, *iter;
    int lfd = -1;

#ifdef O_CLOEXEC
    if (flags & ~(IN_CLOEXEC|O_CLOEXEC|IN_NONBLOCK|O_NONBLOCK|IN_DIRECT)) {
#else
    if (flags & ~(IN_CLOEXEC|IN_NONBLOCK|O_NONBLOCK|IN_DIRECT)) {
#endif
        errno = EINVAL;
        return -1;
    }

    if (atomic_fetch_add (&nworkers, 1) >= max_workers) {
        errno = EMFILE;
        atomic_fetch_sub (&nworkers, 1);
        return -1;
    }

    wrk = worker_create (flags);
    if (wrk == NULL) {
        atomic_fetch_sub (&nworkers, 1);
        return -1;
    }

    lfd = wrk->io[INOTIFY_FD];

    /* We can face into situation when there are two workers with the same
     * inotify FDs. It usually occurs when a worker fd has been closed but
     * the worker has not been removed from a list yet. The fd is free, and
     * when we create a new worker, we can * receive the same fd. So check
     * for duplicates and remove them now. */
    workerset_wlock ();
    SLIST_FOREACH (iter, &workers, next) {
        if (iter->io[INOTIFY_FD] == lfd) {
            iter->io[INOTIFY_FD] = -1;
            perror_msg (("Collision found: fd %d", lfd));
            break;
        }
    }

    SLIST_INSERT_HEAD (&workers, wrk, next);
    workerset_unlock ();

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
                   uint32_t    mask)
{
    struct stat st;
    struct worker_cmd cmd;

    if (!is_opened (fd)) {
        return -1;	/* errno = EBADF */
    }

    /*
     * this lstat() call guards worker from incorrectly specified path.
     * E.g, it prevents catching of SIGSEGV when pathname points outside
     * of the process's accessible address space
     */
    if (lstat (name, &st) == -1) {
        perror_msg (("failed to lstat watch %s",
                     errno != EFAULT ? name : "<bad addr>"));
        return -1;
    }

    if (mask == 0) {
        perror_msg (("Failed to open watch %s. Bad event mask %x", name, mask));
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
                  int wd)
{
    struct worker_cmd cmd;

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
libinotify_set_param (int fd, int param, intptr_t value)
{
    struct worker_cmd cmd;

    /* Apply global parameter right here */
    switch (param) {
    case IN_MAX_USER_INSTANCES:
        if (value < 0 || value > INT_MAX - 1 || fd != -1) {
            errno = EINVAL;
            return -1;
        }
        max_workers = value;
        return 0;

    case IN_SOCKBUFSIZE:
    case IN_MAX_QUEUED_EVENTS:
        /* Or pass per-instance parameters to workers */
        if (!is_opened (fd)) {
            return -1;	/* errno = EBADF */
        }
        worker_cmd_param (&cmd, param, value);
        return worker_exec (fd, &cmd);

    default:
        errno = EINVAL;
    }

    return -1;
}

int libinotify_direct_readv (int fd, struct iovec **events, int size, int no_block)
{
    int nevents;
    struct timespec timeout = {0};
    struct kevent received[size];

    do {
        nevents = kevent (fd, NULL, 0, received, size, no_block ? &timeout : NULL);
    } while (nevents < 0 && !no_block && errno == EINTR);

    if (nevents == -1) {
        perror_msg (("libinotify_direct_drain failed"));
        return -1;
    }

    for (int i = 0; i < nevents; i++) {
#ifdef __DragonFly__
        /* DragonflyBSD does not copy udata */
        events[i] = (struct iovec *)(intptr_t)received[i].data;
#else
        events[i] = (struct iovec *)received[i].udata;
#endif
    }

    return nevents;
}

void libinotify_free_iovec (struct iovec *events)
{
    struct iovec *start = events;

    if (!events)
        return;

    while (events->iov_base) {
        free (events->iov_base);
        events++;
    }

    free(start);
}

int libinotify_direct_close (int fd)
{
    struct worker_cmd cmd;

    worker_cmd_close(&cmd);

    return worker_exec (fd, &cmd);
}

/**
 * Erase a worker from a list of workers.
 *
 * @param[in] wrk A pointer to a worker
 **/
void
worker_erase (struct worker *wrk)
{
    assert (wrk != NULL);

    workerset_wlock ();
    SLIST_REMOVE (&workers, wrk, worker, next);
    wrk->io[INOTIFY_FD] = -1;
    assert (atomic_load (&nworkers) > 0);
    atomic_fetch_sub (&nworkers, 1);
    workerset_unlock ();
}

/**
 * Execute command in context of working thread.
 *
 * @param[in] fd  Inotify instance file descriptor.
 * @param[in] cmd Pointer to #worker_cmd
 * @return 0 on success, -1 on failure with errno set.
 **/
static int
worker_exec (int fd, struct worker_cmd *cmd)
{
    struct worker *wrk;

    workerset_rlock ();

    /* look up for an appropriate worker */
    SLIST_FOREACH (wrk, &workers, next) {
        if (wrk->io[INOTIFY_FD] == fd) {
            worker_ref (wrk);
            workerset_unlock ();
            worker_cmd_lock (wrk);
            if (wrk->io[INOTIFY_FD] != fd) {
                /* RACE: worker thread overwrote inotify descriptor in between
                   obtaining pointer on wrk and locking its mutex. */
                perror_msg (("race detected. fd: %d", fd));
                worker_cmd_unlock (wrk);
                worker_unref (wrk);
                errno = EBADF;
                return -1;
            }

            cmd->retval = -1;
            cmd->error = EBADF;

            if (worker_notify (wrk, cmd) >= 0) {
                worker_wait (wrk);
            }

            worker_cmd_unlock (wrk);
            worker_unref (wrk);
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
