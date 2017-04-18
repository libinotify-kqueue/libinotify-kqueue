/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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

#ifndef __COMPAT_H__
#define __COMPAT_H__

#include "config.h"

#include <sys/param.h>
#include <sys/types.h>

#ifdef HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>  /* RB tree macroses */
#else
#include "compat/tree.h"
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#include <inttypes.h>
#endif

#ifndef __cplusplus /* requires stdbool.h */
#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#elif defined (HAVE_COMPAT_STDATOMIC_H)
#include "compat/stdatomic.h"
#else
#include "compat/ik_atomic.h"
#endif
#endif

#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#ifdef NATIVE_SEMAPHORES
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#define sem_t dispatch_semaphore_t
#define sem_init(sem, pshared, value) ({ \
    int ret_; \
    *(sem) = dispatch_semaphore_create(value); \
    ret_ = (*(sem) == NULL) ? -1 : 0; \
    ret_; \
})
#define sem_wait(sem) (dispatch_semaphore_wait(*(sem), DISPATCH_TIME_FOREVER) * 0)
#define sem_post(sem) (dispatch_semaphore_signal(*(sem)) * 0)
#define sem_destroy(sem) ({ \
    while (dispatch_semaphore_wait(*(sem), DISPATCH_TIME_NOW) == 0) {} \
    dispatch_release(*(sem)); \
    0; \
})
#else
#include <semaphore.h>
#endif
#else /* !NATIVE_SEMAPHORES */
typedef struct {
    int val;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} sem_t;
#define sem_init(sem, pshared, value) ({ \
    pthread_mutex_init(&(sem)->mutex, NULL); \
    pthread_cond_init(&(sem)->cond, NULL); \
    (sem)->val = value; \
    0; \
})
#define sem_wait(sem) ({ \
    pthread_mutex_lock(&(sem)->mutex); \
    while ((sem)->val == 0) { \
        pthread_cond_wait(&(sem)->cond, &(sem)->mutex); \
    } \
    --(sem)->val; \
    pthread_mutex_unlock(&(sem)->mutex); \
    0; \
})
#define sem_post(sem) ({ \
    pthread_mutex_lock(&(sem)->mutex); \
    ++(sem)->val; \
    pthread_cond_broadcast(&(sem)->cond); \
    pthread_mutex_unlock(&(sem)->mutex); \
    0; \
})
#define sem_destroy(sem) ({ \
    pthread_cond_destroy(&(sem)->cond); \
    pthread_mutex_destroy(&(sem)->mutex); \
})
#endif /* !NATIVE_SEMAPHORES */

#ifndef DTTOIF
#define DTTOIF(dirtype) ((dirtype) << 12)
#endif

#ifndef SIZE_MAX
#define SIZE_MAX SIZE_T_MAX
#endif

#ifndef nitems
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#endif

/* FreeBSD 4.x doesn't have IOV_MAX exposed. */
#ifndef IOV_MAX
#if defined(__FreeBSD__) || defined(__APPLE__)
#define IOV_MAX 1024
#endif
#endif

#ifndef AT_FDCWD
#define AT_FDCWD		-100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW	0x200 /* Do not follow symbolic links */
#endif

#ifndef HAVE_PTHREAD_BARRIER
typedef struct {
    int count;               /* the number of threads to wait on a barrier */
    volatile int entered;    /* the number of threads entered on a barrier */
    volatile int sleeping;   /* the number of threads still sleeping */

    pthread_mutex_t mtx;     /* barrier's internal mutex.. */
    pthread_cond_t  cnd;     /* ..and a condition variable */
} pthread_barrier_t;

/* barrier attributes are not supported */
typedef void pthread_barrierattr_t;
#endif

__BEGIN_DECLS

#ifndef HAVE_PTHREAD_BARRIER
void pthread_barrier_init    (pthread_barrier_t *impl,
                              const pthread_barrierattr_t *attr,
                              unsigned count);
void pthread_barrier_wait    (pthread_barrier_t *impl);
void pthread_barrier_destroy (pthread_barrier_t *impl);
#endif

#ifndef HAVE_ATFUNCS
char *fd_getpath_cached (int fd);
char *fd_concat (int fd, const char *file);
#endif
#ifndef HAVE_OPENAT
int openat (int fd, const char *path, int flags, ...);
#endif
#ifndef HAVE_FDOPENDIR
DIR *fdopendir (int fd);
#endif
#ifndef HAVE_FSTATAT
int fstatat (int fd, const char *path, struct stat *buf, int flag);
#endif

__END_DECLS

#endif /* __COMPAT_H__ */
