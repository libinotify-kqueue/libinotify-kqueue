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

#ifndef __WORKER_H__
#define __WORKER_H__

#include <sys/uio.h> /* iovec */

#include <pthread.h>
#include <stdint.h>

typedef struct worker worker;

#include "compat.h"
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

/**
 * This structure represents a user call to the inotify API.
 * It is also used to synchronize a user thread with a worker thread.
 **/
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

void worker_cmd_init    (worker_cmd *cmd);
void worker_cmd_add     (worker_cmd *cmd, const char *filename, uint32_t mask);
void worker_cmd_remove  (worker_cmd *cmd, int watch_id);
void worker_cmd_wait    (worker_cmd *cmd);
void worker_cmd_release (worker_cmd *cmd);

struct worker {
    int kq;                /* kqueue descriptor */
    volatile int io[2];    /* a socket pair */
    struct iovec *iov;     /* inotify events to send */
    int iovcnt;            /* number of events enqueued */
    int iovalloc;          /* number of iovs allocated */
    pthread_t thread;      /* worker thread */
    worker_sets sets;      /* filenames, etc */
    volatile int closed;   /* closed flag */

    pthread_mutex_t mutex; /* worker mutex */
    worker_cmd cmd;        /* operation to perform on a worker */
};


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
void    worker_remove_many    (worker *wrk, watch *parent, const dep_list* items, int remove_self);
void    worker_remove_watch   (worker *wrk, watch *parent, const dep_item* item);

#endif /* __WORKER_H__ */
