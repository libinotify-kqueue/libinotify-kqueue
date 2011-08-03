#include <sys/event.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
#include <assert.h>
#include <stdio.h>
#include <dirent.h>

#include "inotify.h"
#include "utils.h"
#include "conversions.h"
#include "worker-thread.h"
#include "worker.h"


static void
worker_update_flags (worker *wrk, watch *w, uint32_t flags);


void
worker_cmd_reset (worker_cmd *cmd)
{
    assert (cmd != NULL);

    free (cmd->add.filename);
    memset (cmd, 0, sizeof (worker_cmd));
}

worker*
worker_create ()
{
    worker* wrk = calloc (1, sizeof (worker));

    if (wrk == NULL) {
        perror ("Failed to create a new worker");
        goto failure;
    }

    wrk->kq = kqueue ();
    if (wrk->kq == -1) {
        perror ("Failed to create a new kqueue");
        goto failure;
    }

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, wrk->io) == -1) {
        perror ("Failed to create a socket pair");
        goto failure;
    }

    worker_sets_init (&wrk->sets, wrk->io[KQUEUE_FD]);
    pthread_mutex_init (&wrk->mutex, NULL);

    /* create a run a worker thread */
    if (pthread_create (&wrk->thread, NULL, worker_thread, wrk) != 0) {
        perror ("Failed to start a new worker thread");
        goto failure;
    }

    return wrk;
    
    failure:
    if (wrk != NULL) {
        worker_free (wrk);
    }
    return NULL;
}


void
worker_free (worker *wrk)
{
    assert (wrk != NULL);

    worker_sets_free (&wrk->sets);
    free (wrk);
}

static int
worker_add_dependencies (worker        *wrk,
                         struct kevent *event,
                         watch         *parent)
{
    assert (wrk != NULL);
    assert (parent != NULL);
    assert (parent->type == WATCH_USER);
    assert (event != NULL);

    parent->deps = dl_listing (parent->filename);

    {   dep_list *iter = parent->deps;
        while (iter != NULL) {
            char *path = path_concat (parent->filename, iter->path);
            watch *neww = worker_start_watching (wrk,
                                                 path,
                                                 iter->path,
                                                 parent->flags,
                                                 WATCH_DEPENDENCY);
            if (neww == NULL) {
                perror ("Failed to start watching a dependency\n");
                /* TODO ? */
            }
            neww->parent = parent;
            iter = iter->next;
            free (path);
        }
    }
    return 0;
}

watch*
worker_start_watching (worker     *wrk,
                       const char *path,
                       const char *entry_name,
                       uint32_t    flags,
                       int         type)
{
    assert (wrk != NULL);
    assert (path != NULL);

    int i;

    worker_sets_extend (&wrk->sets, 1);
    i = wrk->sets.length;
    wrk->sets.watches[i] = calloc (1, sizeof (struct watch));
    if (watch_init (wrk->sets.watches[i],
                    type,
                    &wrk->sets.events[i],
                    path,
                    entry_name,
                    flags,
                    i)
        == -1) {
        perror ("Failed to initialize a user watch\n");
        // TODO: error
        return NULL;
    }
    ++wrk->sets.length;

    printf ("Starting watching on %s, index %d\n", path, i);

    if (type == WATCH_USER && wrk->sets.watches[i]->is_directory) {
        worker_add_dependencies (wrk, &wrk->sets.events[i], wrk->sets.watches[i]);
    }
    return wrk->sets.watches[i];
}

int
worker_add_or_modify (worker     *wrk,
                      const char *path,
                      uint32_t    flags)
{
    assert (path != NULL);
    assert (wrk != NULL);
    // TODO: a pointer to sets?
    assert (wrk->sets.events != NULL);
    assert (wrk->sets.watches != NULL);

    int i = 0;
    // look up for an entry with this filename
    for (i = 1; i < wrk->sets.length; i++) {
        const char *evpath = wrk->sets.watches[i]->filename;
        assert (evpath != NULL);

        if (wrk->sets.watches[i]->type == WATCH_USER &&
            strcmp (path, evpath) == 0) {
            worker_update_flags (wrk, wrk->sets.watches[i], flags);
            return i;
        }
    }

    // add a new entry if path is not found
    watch *w = worker_start_watching (wrk, path, NULL, flags, WATCH_USER); // TODO: magic number
    return (w != NULL) ? w->fd : -1;
}


int
worker_remove (worker *wrk,
               int     id)
{
    assert (wrk != NULL);
    assert (id != -1);

    int i;
    for (i = 1; i < wrk->sets.length; i++) {
        if (wrk->sets.events[i].ident == id) {
            worker_remove_many (wrk,
                                wrk->sets.watches[i],
                                wrk->sets.watches[i]->deps,
                                1);
            break;
        }
    }
    /* Assume always success */
    return 0;
}



static void
worker_update_flags (worker *wrk, watch *w, uint32_t flags)
{
    assert (w != NULL);
    assert (w->event != NULL);

    printf ("Updating flags!\n");

    w->flags = flags;
    w->event->fflags = inotify_to_kqueue (flags, w->is_directory);

    /* Propagate the flag changes also on all dependent watches */
    if (w->deps) {
        uint32_t ino_flags = inotify_to_kqueue (flags, 0);

        /* Yes, it is quite stupid to iterate over ALL watches of a worker
         * while we have a linked list of its dependencies.
         * TODO improve it
         */
        int i;
        for (i = 1; i < wrk->sets.length; i++) {
            watch *depw = wrk->sets.watches[i];
            if (depw->parent == w) {
                depw->flags = flags;
                depw->event->fflags = ino_flags;
            }
        }
    }
}


void
worker_remove_many (worker *wrk, watch *parent, dep_list *items, int remove_self)
{
    assert (wrk != NULL);
    assert (parent != NULL);

    dep_list *to_remove = dl_shallow_copy (items);
    dep_list *to_head = to_remove;
    int i, j;

    for (i = 1, j = 1; i < wrk->sets.length; i++) {
        dep_list *iter = to_head;
        dep_list *prev = NULL;
        watch *w = wrk->sets.watches[i];

        if (remove_self && w == parent) {
            /* Remove the parent watch itself. The watch will be freed later,
             * now just remove it from the array */
            continue;
        }

        if (w->parent == parent) {
            while (iter != NULL && strcmp (iter->path, w->filename) != 0) {
                prev = iter;
                iter = iter->next;
            }

            if (iter != NULL) {
                /* At first, remove this entry from a list of files to remove */
                if (prev) {
                    prev->next = iter->next;
                } else {
                    to_head = iter->next;
                }

                /* Then, remove the watch itself */
                watch_free (w);
                continue;
            }
        }

        /* If the control reached here, keep this item */
        if (i != j) {
            wrk->sets.events[j] = wrk->sets.events[i];
            wrk->sets.events[j].udata = j;
            wrk->sets.watches[j] = w;
            wrk->sets.watches[j]->event = &wrk->sets.events[j];
        }
        ++j;
    }

    if (remove_self) {
        watch_free (parent);
    }

    wrk->sets.length -= (i - j);

    // TODO: who will free items?
    // TODO: possible memory corruption here?
    dl_shallow_free (to_remove);
}


void
worker_update_paths (worker *wrk, watch *parent)
{
    assert (wrk != NULL);
    assert (parent != NULL);

    if (parent->deps == NULL) {
        return;
    }

    dep_list *to_update = dl_shallow_copy (parent->deps);
    dep_list *to_head = to_update;
    int i, j;

    for (i = 1, j = 1; i < wrk->sets.length; i++) {
        dep_list *iter = to_head;
        dep_list *prev = NULL;
        watch *w = wrk->sets.watches[i];

        if (to_head == NULL) {
            break;
        }

        if (w->parent == parent) {
            while (iter != NULL && iter->inode != w->inode) {
                prev = iter;
                iter = iter->next;
            }

            if (iter != NULL) {
                if (prev) {
                    prev->next = iter->next;
                } else {
                    to_head = iter->next;
                }
            }

            free (w->filename);
            // TODO: memleak?
            w->filename = strdup (iter->path);
        }
    }
    
    // TODO: possible memory corruption here?
    dl_shallow_free (to_update);
}
