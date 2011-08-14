#ifndef __WORKER_H__
#define __WORKER_H__

#include <pthread.h>
#include <stdint.h>
#include <pthread.h>
#include "worker-thread.h"
#include "worker-sets.h"
#include "dep-list.h"

#define INOTIFY_FD 0
#define KQUEUE_FD  1

typedef enum {
    WCMD_NONE = 0,   /* uninitialized state */
    WCMD_ADD,        /* add or modify a watch */
    WCMD_REMOVE,     /* remove a watch */
} worker_cmd_type_t;

typedef struct worker_cmd {
    worker_cmd_type_t type;
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


void worker_cmd_add    (worker_cmd *cmd, const char *filename, uint32_t mask);
void worker_cmd_remove (worker_cmd *cmd, int watch_id);
void worker_cmd_wait   (worker_cmd *cmd);


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

watch*
worker_start_watching (worker      *wrk,
                       const char  *path,
                       const char  *entry_name,
                       uint32_t     flags,
                       watch_type_t type);

int     worker_add_or_modify  (worker *wrk, const char *path, uint32_t flags);
int     worker_remove         (worker *wrk, int id);

void    worker_update_paths   (worker *wrk, watch *parent);
void    worker_remove_many    (worker *wrk, watch *parent, dep_list* items, int remove_self);

#endif /* __WORKER_H__ */
