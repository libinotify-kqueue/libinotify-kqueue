#include <assert.h>
#include <stdlib.h> /* realloc */
#include <string.h> /* memset */
#include <stddef.h> /* NULL */
#include <fcntl.h>  /* open, fstat */
#include <stdio.h>  /* perror */
#include <dirent.h> /* opendir, readdir, closedir */
#include <sys/event.h>

#include "inotify.h"
#include "worker-sets.h"


#define WS_RESERVED 10

void
worker_sets_init (worker_sets *ws,
                  int          fd)
{
    assert (ws != NULL);

    memset (ws, 0, sizeof (worker_sets));
    worker_sets_extend (ws, 1);

    EV_SET (&ws->events[0],
            fd,
            EVFILT_READ,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            NOTE_LOWAT,
            1,
            0);
    ws->length = 1;
}

int
worker_sets_extend (worker_sets *ws,
                    int          count)
{
    assert (ws != NULL);

    if (ws->length + count > ws->allocated) {
        ws->allocated += (count + WS_RESERVED);

        // TODO: check realloc fails
        ws->events = realloc (ws->events, sizeof (struct kevent) * ws->allocated);
        ws->watches = realloc (ws->watches, sizeof (struct watch *) * ws->allocated);
    }
    return 0;
}

void
worker_sets_free (worker_sets *ws)
{
    assert (ws != NULL);

    /* int i; */
    /* for (i = 0; i < ws->allocated; i++) { */
    /*     free (ws->filenames[i]); */
    /* } */
    /* free (ws->is_user); */
    /* free (ws->is_directory); */
    /* free (ws->events); */
    /* free (ws); */
}
