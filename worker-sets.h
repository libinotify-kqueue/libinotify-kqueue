#ifndef __WORKER_SETS_H__
#define __WORKER_SETS_H__

#include <stdint.h>
#include <sys/types.h> /* size_t */

#include "watch.h"

typedef struct worker_sets {
    struct kevent *events;    /* kevent entries */
    struct watch **watches;   /* appropriate watches with additional info */
    size_t length;            /* size of active entries */
    size_t allocated;         /* size of allocated entries */
} worker_sets;

void worker_sets_init   (worker_sets *ws, int fd);
int  worker_sets_extend (worker_sets *ws, int count);
void worker_sets_free   (worker_sets *ws);


#endif /* __WORKER_SETS_H__ */
