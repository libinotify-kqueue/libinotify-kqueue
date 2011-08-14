#include <sys/event.h>
#include <stddef.h> /* NULL */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h> /* printf */

#include "worker.h"
#include "utils.h"
#include "inotify.h"


#define WORKER_SZ 100
static worker* workers[WORKER_SZ] = {NULL};
static pthread_mutex_t workers_mutex = PTHREAD_MUTEX_INITIALIZER;

INO_EXPORT int
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

INO_EXPORT int
inotify_init1 (int flags) __THROW
{
    // TODO: implementation
    return 0;
}

INO_EXPORT int
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

            worker_cmd_add (&wrk->cmd, name, mask);
            safe_write (wrk->io[INOTIFY_FD], "*", 1);
            worker_cmd_wait (&wrk->cmd);

            // TODO: check error here
            int retval = wrk->cmd.retval;
            pthread_mutex_unlock (&wrk->mutex);
            pthread_mutex_unlock (&workers_mutex);
            return retval;
        }
    }

    pthread_mutex_unlock (&workers_mutex);
    return -1;
}

INO_EXPORT int
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

            worker_cmd_remove (&wrk->cmd, wd);
            safe_write (wrk->io[INOTIFY_FD], "*", 1);
            worker_cmd_wait (&wrk->cmd);

            // TODO: check error here
            int retval = wrk->cmd.retval;
            pthread_mutex_unlock (&wrk->mutex);
            pthread_mutex_unlock (&workers_mutex);
            return retval;
        }
    }
    
    pthread_mutex_unlock (&workers_mutex);
    return 0;
}
