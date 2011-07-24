#include <sys/event.h>
#include <stddef.h> /* NULL */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h> /* printf */

#include "worker.h"
#include "inotify.h"


#define WORKER_SZ 100
static worker* workers[WORKER_SZ] = {NULL};
static pthread_mutex_t workers_mutex = PTHREAD_MUTEX_INITIALIZER;

int
inotify_init (void) __THROW
{
    // TODO: a dynamic structure here
    pthread_mutex_lock (&workers_mutex);
    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i] == NULL) {
            worker *wrk = worker_create ();
            if (wrk != NULL) {
                workers[i] = wrk;
                pthread_mutex_unlock (&workers_mutex);
                return wrk->io[INOTIFY_FD];
            }
        }
    }

    // TODO: errno is set when an original inotify_init fails
    pthread_mutex_unlock (&workers_mutex);
    return -1;
}

int
inotify_init1 (int flags) __THROW
{
    // TODO: implementation
    return 0;
}

int
inotify_add_watch (int         fd,
                   const char *name,
                   uint32_t    mask) __THROW
{
    /* look up for an appropriate thread */
    pthread_mutex_lock (&workers_mutex);

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i]->io[INOTIFY_FD] == fd) {
            worker *wrk = workers[i];

            pthread_mutex_lock (&wrk->mutex);

            // TODO: hide these details
            worker_cmd_reset (&wrk->cmd);
            wrk->cmd.type = WCMD_ADD;
            wrk->cmd.add.filename = strdup (name);
            wrk->cmd.add.mask = mask;
            pthread_barrier_init (&wrk->cmd.sync, NULL, 2);

            write (wrk->io[INOTIFY_FD], "*", 1); // TODO: EINTR
            pthread_barrier_wait (&wrk->cmd.sync);

            // TODO: hide these details too
            pthread_barrier_destroy (&wrk->cmd.sync);

            // TODO: check error here
            pthread_mutex_unlock (&workers_mutex);
            return wrk->cmd.retval;
        }
    }

    // TODO: unlock workers earlier?
    pthread_mutex_unlock (&workers_mutex);
    return -1;
}

int
inotify_rm_watch (int fd,
                  int wd) __THROW
{
    assert (fd != -1);
    assert (wd != -1);
    
    pthread_mutex_lock (&workers_mutex);

    int i;
    for (i = 0; i < WORKER_SZ; i++) {
        if (workers[i]->io[INOTIFY_FD] == fd) {
            worker *wrk = workers[i];
            pthread_mutex_lock (&wrk->mutex);

            // TODO: hide these details
            worker_cmd_reset (&wrk->cmd);
            wrk->cmd.type = WCMD_REMOVE;
            wrk->cmd.rm_id = fd;
            pthread_barrier_init (&wrk->cmd.sync, NULL, 2);

            write (wrk->io[INOTIFY_FD], "*", 1); // TODO: EINTR
            pthread_barrier_wait (&wrk->cmd.sync);

            // TODO: hide these details too
            pthread_barrier_destroy (&wrk->cmd.sync);

            // TODO: check error here
            // TODO: unlock workers earlier?
            pthread_mutex_unlock (&workers_mutex);
            return -1; // TODO: obtain return value
        }
    }
    
    pthread_mutex_unlock (&workers_mutex);
    return 0;
}
