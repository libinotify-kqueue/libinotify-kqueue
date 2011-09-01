/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

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

#include <fcntl.h>  /* open */
#include <unistd.h> /* close */
#include <string.h> /* strdup */
#include <stdlib.h> /* free */
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h> /* stat */

#include "utils.h"
#include "conversions.h"
#include "watch.h"
#include "sys/inotify.h"

/**
 * Get some file information by its file descriptor.
 *
 * @param[in]  fd      A file descriptor.
 * @param[out] is_dir  A flag indicating directory.
 * @param[out] inode   A file's inode number.
 **/
static void
_file_information (int fd, int *is_dir, ino_t *inode)
{
    assert (fd != -1);
    assert (is_dir != NULL);
    assert (inode != NULL);

    struct stat st;
    memset (&st, 0, sizeof (struct stat));

    if (fstat (fd, &st) == -1) {
        perror_msg ("fstat failed, assuming it is just a file");
        return;
    }

    *is_dir = ((st.st_mode & S_IFDIR) == S_IFDIR) ? 1 : 0;
    *inode = st.st_ino;
}



#define DEPS_EXCLUDED_FLAGS \
    ( IN_MOVED_FROM \
    | IN_MOVED_TO \
    | IN_MOVE_SELF \
    | IN_DELETE_SELF \
    )

/**
 * Initialize a watch.
 *
 * @param[in,out] w          A pointer to a watch.
 * @param[in]     watch_type The type of the watch.
 * @param[in,out] kv         A pointer to the associated kqueue event.
 * @param[in]     path       A full path to a file.
 * @param[in]     entry_name A name of a watched file (for dependency watches).
 * @param[in]     flags      A combination of the inotify watch flags.
 * @param[in]     index      The index of a watch in the worker sets.
 * @return 0 on success, -1 on failure.
 **/
int
watch_init (watch         *w,
	    watch_type_t   watch_type,
	    struct kevent *kv,
	    const char    *path,
	    const char    *entry_name,
	    uint32_t       flags,
	    int            index)
{
    assert (w != NULL);
    assert (kv != NULL);
    assert (path != NULL);

    memset (w, 0, sizeof (watch));
    memset (kv, 0, sizeof (struct kevent));

    int fd = open (path, O_RDONLY);
    if (fd == -1) {
        perror_msg ("Failed to open a file");
        return -1;
    }

    if (watch_type == WATCH_DEPENDENCY) {
        flags &= ~DEPS_EXCLUDED_FLAGS;
    }

    w->type = watch_type;
    w->flags = flags;
    w->filename = strdup (watch_type == WATCH_USER ? path : entry_name);
    w->fd = fd;
    w->event = kv;

    int is_dir = 0;
    _file_information (fd, &is_dir, &w->inode);
    w->is_directory = (watch_type == WATCH_USER ? is_dir : 0);

    EV_SET (kv,
            fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            inotify_to_kqueue (flags, w->is_directory),
            0,
            INDEX_TO_UDATA (index));

    return 0;
}

/**
 * Reopen a watch.
 *
 * @param[in] w A pointer to a watch.
 * @return 0 on success, -1 on failure.
 **/
int
watch_reopen (watch *w)
{
    assert (w != NULL);
    assert (w->parent != NULL);
    assert (w->event != NULL);
    close (w->fd);

    char *filename = path_concat (w->parent->filename, w->filename);
    if (filename == NULL) {
        perror_msg ("Failed to create a filename to make reopen");
        return -1;
    }

    int fd = open (filename, O_RDONLY);
    if (fd == -1) {
        perror_msg ("Failed to reopen a file");
        free (filename);
        return -1;
    }

    w->fd = fd;
    w->event->ident = fd;

    /* Actually, reopen happens only for dependencies. */
    int is_dir = 0;
    _file_information (fd, &is_dir, &w->inode);
    w->is_directory = (w->type == WATCH_USER ? is_dir : 0);

    free (filename);
    return 0;
}

/**
 * Free a watch and all the associated memory.
 *
 * @param[in] w A pointer to a watch.
 **/
void
watch_free (watch *w)
{
    assert (w != NULL);
    close (w->fd);
    if (w->type == WATCH_USER && w->is_directory && w->deps) {
        dl_free (w->deps);
    }
    free (w->filename);
    free (w);
}
