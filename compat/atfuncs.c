/*******************************************************************************
  Copyright (c) 2014 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <sys/param.h> /* MAXPATHLEN */
#include <sys/types.h>
#include <sys/stat.h>  /* stat */

#include <assert.h>
#include <dirent.h> /* opendir */
#include <errno.h>  /* errno */
#include <fcntl.h>  /* fcntl */
#include <limits.h> /* PATH_MAX */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <unistd.h> /* fchdir */

#include "compat.h"
#include "config.h"

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
    char *path = NULL;
    struct stat st;

    assert (fd != -1);

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
    {
        DIR *save = opendir(".");

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
    dirpath_t *newdp, *olddp;

    assert (path != NULL);

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
char *
fd_getpath_cached (int fd)
{
    dirpath_t find, *dir;
    struct stat st1, st2;
    char *path;

    assert (fd != -1);

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
    size_t dir_len, file_len, alloc_sz;
    char *path;

    assert (dir != NULL);
    assert (file != NULL);

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
char*
fd_concat (int fd, const char *file)
{
    char *path = NULL;
    struct stat st;

    if (fd == AT_FDCWD || file[0] == '/') {

        if (stat (file, &st) != -1
          && S_ISDIR (st.st_mode)
          && (path = malloc (PATH_MAX + 1)) != NULL
          && realpath (file, path) == path) {

            pthread_mutex_lock (&dirs_mtx);
            dir_insert (path, st.st_ino, st.st_dev);
            pthread_mutex_unlock (&dirs_mtx);
        } else {
            if (path == NULL) {
                path = strdup (file);
            } else {
                strlcpy (path, file, PATH_MAX + 1);
            }
        }
    } else {

        char *dirpath = fd_getpath_cached (fd);
        if (dirpath != NULL) {
            path = path_concat (dirpath, file);
        }
    }

    return path;
}
