/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

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
#include <stdio.h>

#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"

#include "utils.h"
#include "worker.h"


#define WORKER_SZ 100
static worker* volatile workers[WORKER_SZ] = {NULL};
static pthread_mutex_t workers_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Create a new inotify instance.
 *
 * This function will create a new inotify instance (actually, a worker
 * with its own thread). To destroy the instance, just close its file
 * descriptor.
 *
 * @return  -1 on failure, a file descriptor on success.
 **/
INO_EXPORT int
inotify_init (void) __THROW
{
    pthread_mutex_lock (&workers_mutex);

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i] == NULL) {
            worker *wrk = worker_create ();
            if (wrk != NULL) {
                workers[i] = wrk;

                /* We can face into situation when there are two workers with
                 * the same inotify FDs. It usually occurs when a worker fd has
                 * been closed but the worker has not been removed from a list
                 * yet. The fd is free, and when we create a new worker, we can
                 * receive the same fd. So check for duplicates and remove them
                 * now. */
                int j, lfd = wrk->io[INOTIFY_FD];
                for (j = 0; j < WORKER_SZ; j++) {
                    worker *jw = workers[j];
                    if (jw != NULL && jw->io[INOTIFY_FD] == lfd && jw != wrk) {
                        workers[j] = NULL;
                        perror_msg ("Collision found: fd %d", lfd);
                    }
                }

                pthread_mutex_unlock (&workers_mutex);
                return wrk->io[INOTIFY_FD];
            }
        }
    }

    pthread_mutex_unlock (&workers_mutex);
    return -1;
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
INO_EXPORT int
inotify_add_watch (int         fd,
                   const char *name,
                   uint32_t    mask) __THROW
{
    pthread_mutex_lock (&workers_mutex);

    /* look up for an appropriate worker */
    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        worker *wrk = workers[i];
        if (wrk != NULL && wrk->io[INOTIFY_FD] == fd && wrk->closed == 0
            && is_opened (wrk->io[INOTIFY_FD])) {
            pthread_mutex_lock (&wrk->mutex);

            if (wrk->closed) {
                worker_free (wrk);
                pthread_mutex_unlock (&wrk->mutex);
                free (wrk);

                pthread_mutex_unlock (&workers_mutex);
                return -1;
            }

            worker_cmd_add (&wrk->cmd, name, mask);
            safe_write (wrk->io[INOTIFY_FD], "*", 1);

            worker_cmd_wait (&wrk->cmd);

            int retval = wrk->cmd.retval;
            pthread_mutex_unlock (&wrk->mutex);

            if (wrk->closed) {
                worker_free (wrk);
                free (wrk);
            }

            pthread_mutex_unlock (&workers_mutex);
            return retval;
        }
    }

    pthread_mutex_unlock (&workers_mutex);
    return -1;
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
INO_EXPORT int
inotify_rm_watch (int fd,
                  int wd) __THROW
{
    assert (fd != -1);
    assert (wd != -1);
    
    pthread_mutex_lock (&workers_mutex);

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        worker *wrk = workers[i];
        if (wrk != NULL && wrk->io[INOTIFY_FD] == fd && wrk->closed == 0
            && is_opened (wrk->io[INOTIFY_FD])) {
            pthread_mutex_lock (&wrk->mutex);

            if (wrk->closed) {
                worker_free (wrk);
                pthread_mutex_unlock (&wrk->mutex);
                free (wrk);

                pthread_mutex_unlock (&workers_mutex);
                return -1;
            }

            worker_cmd_remove (&wrk->cmd, wd);
            safe_write (wrk->io[INOTIFY_FD], "*", 1);
            worker_cmd_wait (&wrk->cmd);

            int retval = wrk->cmd.retval;
            pthread_mutex_unlock (&wrk->mutex);

            if (wrk->closed) {
                worker_free (wrk);
                free (wrk);
            }

            pthread_mutex_unlock (&workers_mutex);
            return retval;
        }
    }

    pthread_mutex_unlock (&workers_mutex);
    return 0;
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
            workers[i] = NULL;
            break;
        }
    }
}
