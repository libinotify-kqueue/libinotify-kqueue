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

#include <assert.h>
#include <stdlib.h> /* realloc */
#include <string.h> /* memset */
#include <stddef.h> /* NULL */
#include <fcntl.h>  /* open, fstat */
#include <dirent.h> /* opendir, readdir, closedir */
#include <sys/types.h>
#include <sys/event.h>

#include "sys/inotify.h"

#include "utils.h"
#include "worker-sets.h"


#define WS_RESERVED 10

/**
 * Initialize the worker sets.
 *
 * @param[in] ws A pointer to the worker sets.
 * @param[in] fd A control file descriptor.
 * @return 0 on success, -1 on failure.
 **/
int
worker_sets_init (worker_sets *ws,
                  int          fd)
{
    assert (ws != NULL);

    memset (ws, 0, sizeof (worker_sets));
    if (worker_sets_extend (ws, 1) == -1) {
        perror_msg ("Failed to initialize worker sets");
        return -1;
    }

    EV_SET (&ws->events[0],
            fd,
            EVFILT_READ,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            NOTE_LOWAT,
            1,
            0);
    ws->length = 1;
    return 0;
}

/**
 * Extend the memory allocated for the worker sets.
 *
 * @param[in] ws    A pointer to the worker sets.
 * @param[in] count The number of items to grow.
 * @return 0 on success, -1 on error.
 **/
int
worker_sets_extend (worker_sets *ws,
                    int          count)
{
    assert (ws != NULL);

    if (ws->length + count > ws->allocated) {
        long to_allocate = ws->allocated + count + WS_RESERVED;

        void *ptr = NULL;
        ptr = realloc (ws->events, sizeof (struct kevent) * to_allocate);
        if (ptr == NULL) {
            perror_msg ("Failed to extend events memory in the worker sets");
            return -1;
        }
        ws->events = ptr;

        ptr = realloc (ws->watches, sizeof (struct watch *) * to_allocate);
        if (ptr == NULL) {
            perror_msg ("Failed to extend watches memory in the worker sets");
            return -1;
        }
        ws->watches = ptr;
        ws->watches[0] = NULL;

        ws->allocated = to_allocate;
    }
    return 0;
}

/**
 * Free the memory allocated for the worker sets.
 *
 * @param[in] ws A pointer the the worker sets.
 **/
void
worker_sets_free (worker_sets *ws)
{
    assert (ws != NULL);
    assert (ws->events != NULL);
    assert (ws->watches != NULL);

    size_t i;
    for (i = 0; i < ws->length; i++) {
        if (ws->watches[i] != NULL) {
            watch_free (ws->watches[i]);
        }
    }

    free (ws->events);
    free (ws->watches);
    memset (ws, 0, sizeof (worker_sets));
}
