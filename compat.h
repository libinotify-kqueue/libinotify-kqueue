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

#ifndef __COMPAT_H__
#define __COMPAT_H__

#include "config.h"

#include <sys/param.h>
#include <sys/types.h>

#ifdef BUILD_LIBRARY
#include <sys/queue.h>
#endif

#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>  /* RB tree macroses */
#else
#include "compat/tree.h"
#endif

#if defined (HAVE_STATFS)
#include <sys/mount.h> /* fstatfs */
#define STATFS statfs
#define FSTATFS(fd, buf) fstatfs((fd), (buf))
#elif defined (HAVE_STATVFS)
#include <sys/statvfs.h> /* fstatvfs */
#define STATFS statvfs
#define FSTATFS(fd, buf) fstatvfs((fd), (buf))
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

/*
 * Minimal pthread condition variable-based POSIX semaphore shim.
 * Used as neither Darwin nor valgrind supports POSIX semafores.
 */
typedef struct {
    int val;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ik_sem_t;
static inline int
ik_sem_init (ik_sem_t *sem, int pshared, int value)
{
    pthread_mutex_init (&sem->mutex, NULL);
    pthread_cond_init (&sem->cond, NULL);
    sem->val = value;
    return 0;
}
static inline int
ik_sem_wait (ik_sem_t *sem)
{
    pthread_mutex_lock (&sem->mutex);
    while (sem->val == 0) {
        pthread_cond_wait (&sem->cond, &sem->mutex);
    }
    --sem->val;
    pthread_mutex_unlock (&sem->mutex);
    return 0;
}
static inline int
ik_sem_post (ik_sem_t *sem)
{
    pthread_mutex_lock (&sem->mutex);
    ++sem->val;
    pthread_cond_broadcast (&sem->cond);
    pthread_mutex_unlock (&sem->mutex);
    return 0;
}
static inline int
ik_sem_destroy (ik_sem_t *sem)
{
    pthread_cond_destroy (&sem->cond);
    pthread_mutex_destroy (&sem->mutex);
    return 0;
}

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
#ifndef HAVE_FDCLOSEDIR
int fdclosedir (DIR *dir);
#endif
#ifndef HAVE_FSTATAT
int fstatat (int fd, const char *path, struct stat *buf, int flag);
#endif

__END_DECLS

#endif /* __COMPAT_H__ */
