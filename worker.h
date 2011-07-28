#ifndef __WORKER_H__
#define __WORKER_H__

#include <pthread.h>
#include <stdint.h>
#include <pthread.h>
#include "worker-thread.h"
#include "worker-sets.h"


#define INOTIFY_FD 0
#define KQUEUE_FD  1


typedef struct worker_cmd {
    enum {
        WCMD_NONE = 0,   /* uninitialized state */
        WCMD_ADD,
        WCMD_REMOVE,
    } type;

    int retval;

    union {
        struct {
            char *filename;
            uint32_t mask;
        } add;

        int rm_id;
    };

    pthread_barrier_t sync;
} worker_cmd;


void worker_cmd_reset (worker_cmd *cmd);


typedef struct {
    int kq;                /* kqueue descriptor */
    int io[2];             /* a socket pair */
    pthread_t thread;      /* worker thread */
    worker_sets sets;      /* kqueue events, filenames, etc */

    pthread_mutex_t mutex; /* worker mutex */
    worker_cmd cmd;        /* operation to perform on a worker */
} worker;


worker* worker_create         ();
void    worker_free           (worker *wrk);

// TODO: enum type for scan_deps
watch*  worker_start_watching (worker *wrk, const char *path, uint32_t flags, int dependency);
int     worker_add_or_modify  (worker *wrk, const char *path, uint32_t flags);
int     worker_remove         (worker *wrk, int id);


#endif /* __WORKER_H__ */
