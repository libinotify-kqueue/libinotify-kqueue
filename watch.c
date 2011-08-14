#include <fcntl.h>  /* open */
#include <unistd.h> /* close */
#include <string.h> /* strdup */
#include <stdio.h>  /* perror */
#include <stdlib.h> /* free */
#include <assert.h>

#include "conversions.h"
#include "watch.h"
#include "inotify.h"

static void
_file_information (int fd, int *is_dir, ino_t *inode)
{
    assert (fd != -1);
    assert (is_dir != NULL);
    assert (inode != NULL);

    struct stat st;
    memset (&st, 0, sizeof (struct stat));

    if (fstat (fd, &st) == -1) {
        perror ("fstat failed, assuming it is just a file");
        return;
    }

    *is_dir = ((st.st_mode & S_IFDIR) == S_IFDIR) ? 1 : 0;
    *inode = st.st_ino;
}



// TODO: reuse inotify_to_kqueue?
#define DEPS_EXCLUDED_FLAGS \
    ( IN_MOVED_FROM \
    | IN_MOVED_TO \
    | IN_MOVE_SELF \
    | IN_DELETE_SELF \
    )


int watch_init (watch         *w,
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

    /* TODO: EINTR? */
    int fd = open (path, O_RDONLY);
    if (fd == -1) {
        // TODO: error
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
            index);

    return 0;
}

void
watch_free (watch *w)
{
    assert (w != NULL);
    close (w->fd); /* TODO: EINTR? */
    free (w->filename);
    free (w);
}
