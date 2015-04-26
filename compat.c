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

#include <sys/param.h> /* MAXPATHLEN */
#include <sys/types.h>
#include <sys/stat.h>  /* stat */

#include <assert.h>
#include <dirent.h> /* opendir */
#include <errno.h>  /* errno */
#include <fcntl.h>  /* fcntl */
#include <limits.h> /* PATH_MAX */
#include <stdarg.h> /* va_start */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */

#include "compat.h"
#include "config.h"

#ifndef HAVE_PTHREAD_BARRIER
/**
 * Initialize a barrier
 *
 * @param[in] impl   A pointer to barrier
 * @param[in] attr   A barrier attributes (not implemented)
 * @param[in] count  The number of threads to wait on the barrier
 **/
void
pthread_barrier_init (pthread_barrier_t *impl,
                      const pthread_barrierattr_t *attr,
                      unsigned count)
{
    assert (impl != NULL);

    memset (impl, 0, sizeof (pthread_barrier_t));
    impl->count = count;

    pthread_mutex_init (&impl->mtx, NULL);
    pthread_cond_init  (&impl->cnd, NULL);
}


/**
 * Wait on a barrier.
 *
 * If this thread is not the last expected one, it will be blocked
 * until all the expected threads will check in on the barrier.
 * Otherwise the barrier will be marked as passed and all blocked
 * threads will be unlocked.
 *
 * This barrier implementation is based on:
 *   http://siber.cankaya.edu.tr/ozdogan/GraduateParallelComputing.old/ceng505/node94.html
 *
 * @param[in] impl  A pointer to barrier
 **/
void
pthread_barrier_wait (pthread_barrier_t *impl)
{
    while (impl->entered == 0 && impl->sleeping != 0);

    pthread_mutex_lock (&impl->mtx);
    impl->entered++;
    if (impl->entered == impl->count) {
        impl->entered = 0;
        pthread_cond_broadcast (&impl->cnd);
    } else {
        ++impl->sleeping;
        while (pthread_cond_wait (&impl->cnd, &impl->mtx) != 0
               && impl->entered != 0);
        --impl->sleeping;
    }
    pthread_mutex_unlock (&impl->mtx);
}


/**
 * Destroy the barrier and all associated resources.
 *
 * @param[in] impl  A pointer to barrier
 **/
void
pthread_barrier_destroy (pthread_barrier_t *impl)
{
    assert (impl != NULL);

    pthread_cond_destroy  (&impl->cnd);
    pthread_mutex_destroy (&impl->mtx);

    impl->count = 0;
    impl->entered = 0;
    impl->sleeping = 0;
}
#endif /* HAVE_PTHREAD_BARRIER */

#ifdef BUILD_LIBRARY
#ifndef HAVE_ATFUNCS
typedef struct dirpath_t {
    ino_t inode;              /* inode number */
    dev_t dev;                /* device number */
    char *path;               /* full path to inode */
    RB_ENTRY(dirpath_t) link; /* RB tree links */
} dirpath_t;

/* directory path cache */
static RB_HEAD(dp, dirpath_t) dirs = RB_INITIALIZER (&dirs);
/* directory path cache mutex */
static pthread_mutex_t dirs_mtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * Custom comparison function that can compare directory inode values
 * through pointers passed by RB tree functions
 *
 * @param[in] dp1 A pointer to a first directory to compare
 * @param[in] dp2 A pointer to a second directory to compare
 * @return An -1, 0, or +1 if the first inode is considered to be respectively
 *     less than, equal to, or greater than the second one.
 **/
static int
dirpath_cmp (dirpath_t *dp1, dirpath_t *dp2)
{
    if (dp1->dev == dp2->dev)
        return ((dp1->inode > dp2->inode) - (dp1->inode < dp2->inode));
    else
        return ((dp1->dev > dp2->dev) - (dp1->dev < dp2->dev));
}

RB_GENERATE(dp, dirpath_t, link, dirpath_cmp);

/**
 * Returns a pointer to the absolute pathname of the directory by filedes
 * NOTE: It uses unsafe fchdir if no F_GETPATH fcntl have been found
 *
 * @param[in] fd A file descriptor of opened directory
 * @return a pointer to the pathname on success, NULL otherwise
 **/
static char *
fd_getpath (int fd)
{
    assert (fd != -1);

    char *path = NULL;
    DIR *save;
    struct stat st;

    if (fstat (fd, &st) == -1) {
        return NULL;
    }

    if (!S_ISDIR (st.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

    if (st.st_nlink == 0) {
        errno = ENOENT;
        return NULL;
    }

#if defined (F_GETPATH)
    path = malloc (MAXPATHLEN);
    if (path != NULL && fcntl (fd, F_GETPATH, path) == -1) {
        free (path);
        path = NULL;
    }
#elif defined (ENABLE_UNSAFE_FCHDIR)
    /*
     * Using of this code path can be unsafe in multithreading applications as
     * following code do a temporary change of global current working directory
     * via fchdir call. Consider renaming of watched directory as relatively
     * rare operation so catching such a race is unlikely
     */
    save = opendir(".");

    if (fchdir (fd) == 0) {
        path = malloc (PATH_MAX);
        if (path != NULL && getcwd (path, PATH_MAX) == NULL) {
            free (path);
            path = NULL;
        }
    }

    if (save != NULL) {
        int saved_errno = errno;
        fchdir (dirfd (save));
        closedir (save);
        errno = saved_errno;
    }
#endif /* ENABLE_UNSAFE_FCHDIR && F_GETPATH */

    return path;
}

/**
 * Remove directory path from directory path cache.
 * Should be called with dirs_mtx mutex held
 *
 * @param [in] dir A pointer to a directory to remove from cache
 **/
static void
dir_remove (dirpath_t *dir)
{
    if (dir->path != NULL) {
        free (dir->path);
    }
    RB_REMOVE (dp, &dirs, dir);
    free (dir);
}

/**
 * Insert directory path into directory path cache.
 * Should be called with dirs_mtx mutex held
 *
 * @param [in] path  A directory path to be cached
 * @param [in] inode A inode number of cached directory path
 * @param [in] dev   A device number of cached directory path
 * @return A pointer to allocated cache entry
 **/
static dirpath_t *
dir_insert (const char* path, ino_t inode, dev_t dev)
{
    assert (path != NULL);

    dirpath_t *newdp, *olddp;

    newdp = calloc (1, sizeof (dirpath_t));
    if (newdp == NULL) {
        return NULL;
    }

    newdp->inode = inode;
    newdp->dev = dev;

    olddp = RB_FIND (dp, &dirs, newdp);
    if (olddp != NULL) {
        free (newdp);
        if (strcmp (path, olddp->path)) {
            free (olddp->path);
            olddp->path = strdup (path);
        }
        newdp = olddp;
    } else {
        newdp->path = strdup (path);
        RB_INSERT (dp, &dirs, newdp);
    }

    if (newdp->path == NULL) {
        dir_remove (newdp);
        newdp = NULL;
    }

    return newdp;
}

/**
 * Find cached directory path corresponding a given inode number
 *
 * @param[in] fd A file descriptor of opened directory
 * @return a pointer to the pathname on success, NULL otherwise
 **/
static char *
fd_getpath_cached (int fd)
{
    assert (fd != -1);

    dirpath_t find, *dir;
    struct stat st1, st2;
    char *path;

    if (fstat (fd, &st1) == -1) {
        return NULL;
    }

    if (!S_ISDIR (st1.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

    find.inode = st1.st_ino;
    find.dev = st1.st_dev;

    pthread_mutex_lock (&dirs_mtx);

    dir = RB_FIND (dp, &dirs, &find);
    if (dir == NULL || stat (dir->path, &st2) != 0
      || dir->inode != st2.st_ino || dir->dev != st2.st_dev) {

        path = fd_getpath (fd);
        if (path != NULL) {
            dir = dir_insert (path, st1.st_ino, st1.st_dev);
            free (path);
        } else {
            if (dir != NULL) {
                dir_remove (dir);
                dir = NULL;
            }
        }
    }

    pthread_mutex_unlock (&dirs_mtx);

    return dir != NULL ? dir->path : NULL;
}

/**
 * Create a file path using its name and a path to its directory.
 *
 * @param[in] dir  A path to a file directory. May end with a '/'.
 * @param[in] file File name.
 * @return A concatenated path. Should be freed with free().
 **/
static char*
path_concat (const char *dir, const char *file)
{
    assert (dir != NULL);
    assert (file != NULL);

    size_t dir_len, file_len, alloc_sz;
    char *path;

    dir_len = strlen (dir);
    file_len = strlen (file);
    alloc_sz = dir_len + file_len + 2;

    path = malloc (alloc_sz);
    if (path != NULL) {

        strlcpy (path, dir, alloc_sz);

        if (dir[dir_len - 1] != '/') {
            ++dir_len;
            path[dir_len - 1] = '/';
        }

        strlcpy (path + dir_len, file, file_len + 1);
    }

    return path;
}

/**
 * Create a file path using its name and a filedes of its directory.
 *
 * @param[in] fd   A file descriptor of opened directory.
 * @param[in] file File name.
 * @return A concatenated path. Should be freed with free().
 **/
static char*
fd_concat (int fd, const char *file)
{
    char *path = NULL;
    struct stat st;

    if (fd == AT_FDCWD) {

        if (stat (file, &st) != -1
          && S_ISDIR (st.st_mode)
          && (path = realpath (file, NULL)) != NULL) {

            pthread_mutex_lock (&dirs_mtx);
            dir_insert (path, st.st_ino, st.st_dev);
            pthread_mutex_unlock (&dirs_mtx);
        } else {
            path = strdup (file);
        }
    } else {

        char *dirpath = fd_getpath_cached (fd);
        if (dirpath != NULL) {
            path = path_concat (dirpath, file);
        }
    }

    return path;
}
#endif /* !HAVE_ATFUNCS */

#ifndef HAVE_OPENAT
int
openat (int fd, const char *path, int flags, ...)
{
    char *fullpath;
    int newfd, save_errno;
    mode_t mode;
    va_list ap;

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    } else {
        mode = 0;
    }

    fullpath = fd_concat (fd, path);
    if (fullpath == NULL) {
        return -1;
    }

    newfd = open (fullpath, flags, mode);

    save_errno = errno;
    free (fullpath);
    errno = save_errno;

    return newfd;
}
#endif /* HAVE_OPENAT */

#ifndef HAVE_FDOPENDIR
DIR *
fdopendir (int fd)
{
    char *dirpath = fd_getpath_cached (fd);
    if (dirpath == NULL) {
        return NULL;
    }

    return opendir (dirpath);
}
#endif /* HAVE_FDOPENDIR */

#ifndef HAVE_FSTATAT
int
fstatat (int fd, const char *path, struct stat *buf, int flag)
{
    char *fullpath;
    int retval, save_errno;

    fullpath = fd_concat (fd, path);
    if (fullpath == NULL) {
        return -1;
    }

    if (flag & AT_SYMLINK_NOFOLLOW) {
        retval = lstat (fullpath, buf);
    } else {
        retval = stat (fullpath, buf);
    }

    save_errno = errno;
    free (fullpath);
    errno = save_errno;

    return retval;
}
#endif /* HAVE_FSTATAT */
#endif /* BUILD_LIBRARY */
