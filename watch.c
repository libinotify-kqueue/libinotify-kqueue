#include <fcntl.h>  /* open */
#include <string.h> /* strdup */
#include <stdio.h>  /* perror */
#include <assert.h>

#include "conversions.h"
#include "watch.h"


static int
_is_directory (int fd)
{
    assert (fd != -1);

    struct stat st;
    memset (&st, 0, sizeof (struct stat));

    if (fstat (fd, &st) == -1) {
        perror ("fstat failed, assuming it is just a file");
        return 0;
    }

    return (st.st_mode & S_IFDIR) == S_IFDIR;
}

int watch_init (watch         *w,
                int            watch_type,
                struct kevent *kv,
                const char    *path,
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
        // TODO: error
        return -1;
    }

    w->type = watch_type;
    w->flags = flags;
    w->is_directory = (watch_type == WATCH_USER ? _is_directory (fd) : 0);
    w->filename = strdup (path);
    w->fd = fd;

    EV_SET (kv,
            fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            inotify_to_kqueue (flags, w->is_directory),
            0,
            index);

    return 0;

}
