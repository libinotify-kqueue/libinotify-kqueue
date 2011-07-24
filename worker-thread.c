#include <sys/event.h>
#include <stddef.h> /* NULL */
#include <assert.h>
#include <unistd.h> /* write */
#include <stdlib.h> /* calloc */
#include <stdio.h>  /* perror */
#include <string.h> /* memset */

#include "inotify.h"
#include "worker.h"
#include "worker-sets.h"
#include "worker-thread.h"

static uint32_t
kqueue_to_inotify (uint32_t flags)
{
    uint32_t result = 0;

    if (flags & (NOTE_ATTRIB | NOTE_LINK))
        result |= IN_ATTRIB;

    if (flags & (NOTE_WRITE | NOTE_EXTEND)) // TODO: NOTE_MODIFY?
        result |= IN_MODIFY;
    
    return result;
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
    int name_len = strlen (name) + 1;
    *event_len = sizeof (struct inotify_event) + name_len;
    event = calloc (1, *event_len); // TODO: check allocation

    event->wd = wd;
    event->mask = mask;
    event->cookie = cookie;
    event->len = name_len;
    strcpy (event->name, name);

    return event;
}

// TODO: drop unnecessary arguments
void
produce_directory_moves (worker         *wrk,
                         watch          *w,
                         struct kevent  *event,
                         dep_list      **was, // TODO: removed
                         dep_list      **now) // TODO: added
{
    assert (wrk != NULL);
    assert (w != NULL);
    assert (w->parent != NULL);
    assert (event != NULL);
    assert (was != NULL);
    assert (now != NULL);

    dep_list *was_iter = *was;
    dep_list *was_prev = NULL;

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

                ev = create_inotify_event (w->parent->fd,
                                           IN_MOVED_FROM,
                                           cookie,
                                           was_iter->path,
                                           &event_len);
                // TODO: EINTR
                write (wrk->io[KQUEUE_FD], ev, event_len);
                free (ev);

                ev = create_inotify_event (w->parent->fd,
                                           IN_MOVED_TO,
                                           cookie,
                                           now_iter->path,
                                           &event_len);
                // TODO: EINTR
                write (wrk->io[KQUEUE_FD], ev, event_len);
                free (ev);

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
}


// TODO: drop unnecessary arguments
void
produce_directory_changes (worker         *wrk,
                           watch          *w,
                           struct kevent  *event,
                           dep_list       *list,
                           uint32_t        flag) // TODO: added
{
    assert (wrk != NULL);
    assert (w != NULL);
    assert (w->parent != NULL);
    assert (event != NULL);
    assert (flag != 0);

    while (list != NULL) {
        struct inotify_event *ie = NULL;
        int ie_len = 0;
        // TODO: check allocation
        ie = create_inotify_event (w->parent->fd,
                                   flag,
                                   0,
                                   list->path,
                                   &ie_len);

        write (wrk->io[KQUEUE_FD], ie, ie_len);
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

    produce_directory_moves (wrk, w, event, &was, &now);
    produce_directory_changes (wrk, w, event, was, IN_DELETE);
    produce_directory_changes (wrk, w, event, now, IN_CREATE);

    dl_shallow_free (now);
    dl_free (was);
}

void
produce_notifications (worker *wrk, struct kevent *event)
{
    assert (wrk != NULL);
    assert (event != NULL);

    if (wrk->sets.watches[event->udata].type == WATCH_USER) {
        if (wrk->sets.watches[event->udata].is_directory
            && (event->fflags & (NOTE_WRITE | NOTE_EXTEND))) { // TODO: watch's inotify flags here
            produce_directory_diff (wrk, &wrk->sets.watches[event->udata], event);
        }
        // TODO: other types of events on user entries
    } else {
        /* for dependency events, ignore some notifications */
        if (event->fflags & (NOTE_ATTRIB | NOTE_LINK | NOTE_WRITE | NOTE_EXTEND)) {
            struct inotify_event *ie = NULL;
            watch *w = &wrk->sets.watches[event->udata];
            watch *p = w->parent;
            int ev_len;
            ie = create_inotify_event (p->fd,
                                       kqueue_to_inotify (event->fflags),
                                       0,
                                       // TODO: /foo and /foo/ cases
                                       w->filename + 1 + strlen(p->filename),
                                       &ev_len);
            
            // TODO: EINTR
            write (wrk->io[KQUEUE_FD], ie, ev_len);
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
