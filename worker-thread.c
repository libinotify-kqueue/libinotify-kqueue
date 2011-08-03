#include <sys/event.h>
#include <stddef.h> /* NULL */
#include <assert.h>
#include <unistd.h> /* write */
#include <stdlib.h> /* calloc, realloc */
#include <stdio.h>  /* perror */
#include <string.h> /* memset */

#include "utils.h"
#include "conversions.h"
#include "inotify.h"
#include "worker.h"
#include "worker-sets.h"
#include "worker-thread.h"

typedef struct bulk_events {
    void *memory;
    int size; // TODO size_t
} bulk_events;

void
bulk_write (bulk_events *be, void *mem, int size) // TODO size_t
{
    assert (be != NULL);
    assert (mem != NULL);

    be->memory = realloc (be->memory, be->size + size); // TODO check alloc
    memcpy (be->memory + be->size, mem, size);
    be->size += size;
}

void
process_command (worker *wrk)
{
    assert (wrk != NULL);

    // read a byte
    char unused;
    read (wrk->io[KQUEUE_FD], &unused, 1);

    if (wrk->cmd.type == WCMD_ADD) {
        wrk->cmd.retval = worker_add_or_modify (wrk,
                                                wrk->cmd.add.filename,
                                                wrk->cmd.add.mask);
    } else if (wrk->cmd.type == WCMD_REMOVE) {
        wrk->cmd.retval = worker_remove (wrk, wrk->cmd.rm_id);
    } else {
        // TODO: signal error
    }

    // TODO: is the situation when nobody else waits on a barrier possible?
    pthread_barrier_wait (&wrk->cmd.sync);
}


static struct inotify_event*
create_inotify_event (int wd, uint32_t mask, uint32_t cookie, const char *name, int *event_len)
{
    struct inotify_event *event = NULL;
    int name_len = name ? strlen (name) + 1 : 0;
    *event_len = sizeof (struct inotify_event) + name_len;
    event = calloc (1, *event_len); // TODO: check allocation

    event->wd = wd;
    event->mask = mask;
    event->cookie = cookie;
    event->len = name_len;

    if (name) {
        strcpy (event->name, name);
    }

    return event;
}

// TODO: drop unnecessary arguments
int
produce_directory_moves (watch          *w,
                         struct kevent  *event,
                         dep_list      **was, // TODO: removed
                         dep_list      **now, // TODO: added
                         bulk_events    *be)
{
    assert (w != NULL);
    assert (event != NULL);
    assert (was != NULL);
    assert (now != NULL);

    dep_list *was_iter = *was;
    dep_list *was_prev = NULL;

    int moves = 0;

    while (was_iter != NULL) {
        dep_list *now_iter = *now;
        dep_list *now_prev = NULL;

        int matched = 0;
        while (now_iter != NULL) {
            if (was_iter->inode == now_iter->inode) {
                matched = 1;
                uint32_t cookie = was_iter->inode & 0x00000000FFFFFFFF;
                int event_len = 0;
                struct inotify_event *ev;

                // TODO: write directly on bulk events
                ev = create_inotify_event (w->parent->fd, IN_MOVED_FROM, cookie,
                                           was_iter->path,
                                           &event_len);
                bulk_write (be, ev, event_len);
                free (ev);

                ev = create_inotify_event (w->parent->fd, IN_MOVED_TO, cookie,
                                           now_iter->path,
                                           &event_len);
                bulk_write (be, ev, event_len);
                free (ev);

                ++moves;

                if (was_prev) {
                    was_prev->next = was_iter->next;
                } else {
                    *was = was_iter->next;
                }

                if (now_prev) {
                    now_prev->next = now_iter->next;
                } else {
                    *now = now_iter->next;
                }
                // TODO: free smt
                break;
            }
        }

        dep_list *oldptr = was_iter;
        was_iter = was_iter->next;
        if (matched == 0) {
            was_prev = oldptr;
        } else {
            free (oldptr); // TODO: dl_free?
        }
    }

    return (moves > 0);
}


// TODO: drop unnecessary arguments
void
produce_directory_changes (worker         *wrk,
                           watch          *w,
                           struct kevent  *event,
                           dep_list       *list,
                           uint32_t        flag,
                           bulk_events    *be)
{
    assert (wrk != NULL);
    assert (w != NULL);
    assert (event != NULL);
    assert (flag != 0);

    while (list != NULL) {
        struct inotify_event *ie = NULL;
        int ie_len = 0;
        // TODO: check allocation

        ie = create_inotify_event (w->fd, flag, 0, list->path, &ie_len);

        bulk_write (be, ie, ie_len);
        free (ie);

        list = list->next;
    }
}


// TODO: drop unnecessary arguments
void
produce_directory_diff (worker *wrk, watch *w, struct kevent *event)
{
    assert (wrk != NULL);
    assert (w != NULL);
    assert (event != NULL);

    assert (w->type == WATCH_USER);
    assert (w->is_directory);

    dep_list *was = NULL, *now = NULL;

    was = dl_shallow_copy (w->deps);
    dl_shallow_free (w->deps);

    w->deps = dl_listing (w->filename);
    now = dl_shallow_copy (w->deps);

    dl_diff (&was, &now);

    bulk_events be;
    memset (&be, 0, sizeof (bulk_events));
    if (produce_directory_moves (w, event, &was, &now, &be)) {
        worker_update_paths (wrk, w);
    }
    produce_directory_changes (wrk, w, event, was, IN_DELETE, &be);
    produce_directory_changes (wrk, w, event, now, IN_CREATE, &be);

    if (be.memory) {
        // TODO EINTR
        write (wrk->io[KQUEUE_FD], be.memory, be.size);
        free (be.memory);
    }

    {   dep_list *now_iter = now;
        while (now_iter != NULL) {
            char *path = path_concat (w->filename, now_iter->path);
            watch *neww = worker_start_watching (wrk, path, now_iter->path, w->flags, WATCH_DEPENDENCY);
            if (neww == NULL) {
                perror ("Failed to start watching on a new dependency\n");
                /* TODO terminate? */
            }
            neww->parent = w;
            now_iter = now_iter->next;
            free (path);
        }
    }

    worker_remove_many (wrk, w, was, 0);

    dl_shallow_free (now);
    dl_free (was);
}

void
produce_notifications (worker *wrk, struct kevent *event)
{
    assert (wrk != NULL);
    assert (event != NULL);

    watch *w = wrk->sets.watches[event->udata];

    printf ("kevent %d: %s [%s]\n",
            event->udata,
            w->filename,
            (event->fflags & NOTE_DELETE ? "YAY!!!" : "-"));

    if (w->type == WATCH_USER) {
        uint32_t flags = event->fflags;

        if (w->is_directory
            && (flags & (NOTE_WRITE | NOTE_EXTEND))) {
            produce_directory_diff (wrk, w, event);
            flags &= ~(NOTE_WRITE | NOTE_EXTEND);
        }

        if (flags) {
            struct inotify_event *ie = NULL;
            int ev_len;
            ie = create_inotify_event (w->fd,
                                       kqueue_to_inotify (flags, w->is_directory),
                                       0,
                                       NULL,
                                       &ev_len);

            // TODO: EINTR
            write (wrk->io[KQUEUE_FD], ie, ev_len);
            free (ie);
        }
    } else {
        /* for dependency events, ignore some notifications */
        if (event->fflags & (NOTE_ATTRIB | NOTE_LINK | NOTE_WRITE | NOTE_EXTEND)) {
            struct inotify_event *ie = NULL;
            watch *p = w->parent;
            assert (p != NULL);
            int ev_len;
            ie = create_inotify_event
                (p->fd,
                 kqueue_to_inotify (event->fflags, w->is_directory),
                 0,
                 w->filename,
                 &ev_len);
            
            // TODO: EINTR
            write (wrk->io[KQUEUE_FD], ie, ev_len);
            free (ie);
        }
    }
}

void*
worker_thread (void *arg)
{
    assert (arg != NULL);
    worker* wrk = (worker *) arg;

    for (;;) {
        struct kevent received;

        int ret = kevent (wrk->kq,
                          wrk->sets.events,
                          wrk->sets.length,
                          &received,
                          1,
                          NULL);
        if (ret == -1) {
            perror ("kevent failed");
            continue;
        }

        if (received.ident == wrk->io[KQUEUE_FD]) {
            process_command (wrk);
        } else {
            produce_notifications (wrk, &received);
        }
    }
    return NULL;
}
