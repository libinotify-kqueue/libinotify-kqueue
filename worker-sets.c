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


void
dl_print (dep_list *dl)
{
    while (dl != NULL) {
        printf ("%lld:%s ", dl->inode, dl->path);
        dl = dl->next;
    }
    printf ("\n");
}

dep_list*
dl_shallow_copy (dep_list *dl)
{
    assert (dl != NULL);

    dep_list *head = calloc (1, sizeof (dep_list)); // TODO: check allocation
    dep_list *cp = head;
    dep_list *it = dl;

    while (it != NULL) {
        cp->fd = it->fd;
        cp->path = it->path;
        cp->inode = it->inode;
        if (it->next) {
            cp->next = calloc (1, sizeof (dep_list)); // TODO: check allocation
            cp = cp->next;
        }
        it = it->next;
    }

    return head;
}

void
dl_shallow_free (dep_list *dl)
{
    while (dl != NULL) {
        dep_list *ptr = dl;
        dl = dl->next;
        free (ptr);
    }
}

void
dl_free (dep_list *dl)
{
    while (dl != NULL) {
        dep_list *ptr = dl;
        dl = dl->next;

        free (ptr->path);
        free (ptr);
    }
}

dep_list*
dl_listing (const char *path)
{
    assert (path != NULL);

    dep_list *head = calloc (1, sizeof (dep_list)); // TODO: check allocation
    dep_list *prev = NULL;
    DIR *dir = opendir (path);
    if (dir != NULL) {
        struct dirent *ent;

        while ((ent = readdir (dir)) != NULL) {
            if (!strcmp (ent->d_name, ".") || !strcmp (ent->d_name, "..")) {
                continue;
            }

             // TODO: check allocation
            dep_list *iter = (prev == NULL) ? head : calloc (1, sizeof (dep_list));
            iter->path = strdup (ent->d_name);
            iter->inode = ent->d_ino;
            iter->next = NULL;
            if (prev) {
                prev->next = iter;
            }
            prev = iter;
        }

        closedir (dir);
    }
    return head;
}

void dl_diff (dep_list **before, dep_list **after)
{
    assert (before != NULL);
    assert (after != NULL);

    dep_list *before_iter = *before;
    dep_list *before_prev = NULL;

    assert (before_iter != NULL);

    while (before_iter != NULL) {
        dep_list *after_iter = *after;
        dep_list *after_prev = NULL;

        int matched = 0;
        while (after_iter != NULL) {
            if (strcmp (before_iter->path, after_iter->path) == 0) {
                matched = 1;
                /* removing the entry from the both lists */
                if (before_prev) {
                    before_prev->next = before_iter->next;
                } else {
                    *before = before_iter->next;
                }

                if (after_prev) {
                    after_prev->next = after_iter->next;
                } else {
                    *after = after_iter->next;
                }
                free (after_iter); // TODO: dl_free?
                break;
            }
            after_prev = after_iter;
            after_iter = after_iter->next;
        }

        dep_list *oldptr = before_iter;
        before_iter = before_iter->next;
        if (matched == 0) {
            before_prev = oldptr;
        } else {
            free (oldptr); // TODO: dl_free?
        }
    }
}


static uint32_t
inotify_flags_to_kqueue (uint32_t flags, int is_directory)
{
    uint32_t result = 0;
    static const uint32_t NOTE_MODIFIED = (NOTE_WRITE | NOTE_EXTEND);

    if (flags & IN_ATTRIB)
        result |= (NOTE_ATTRIB | NOTE_LINK);
    if (flags & IN_MODIFY)
        result |= NOTE_MODIFIED;
    if (flags & IN_MOVED_FROM && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_MOVED_TO && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_CREATE && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_DELETE && is_directory)
        result |= NOTE_MODIFIED;
    if (flags & IN_DELETE_SELF)
        result |= NOTE_DELETE;
    if (flags & IN_MOVE_SELF)
        result |= NOTE_RENAME;

    return result;
}


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

int
watch_init_user (watch *w, struct kevent *kv, const char *path, uint32_t flags, int index)
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

    w->type = WATCH_USER;
    w->flags = flags;
    w->is_directory = _is_directory (fd);
    w->filename = strdup (path);
    w->fd = fd;
    EV_SET (kv,
            fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            inotify_flags_to_kqueue (flags, w->is_directory),
            0,
            index);

    return 0;
}

int
watch_init_dependency (watch *w, struct kevent *kv, const char *path, uint32_t flags, int index)
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

    w->type = WATCH_DEPENDENCY;
    w->flags = flags;
    w->filename = strdup (path);
    w->fd = fd;

    // TODO: drop some flags from flags.
    // They are actual for a parent watch, but should be modified
    // for dependant ones
    EV_SET (kv,
            fd,
            EVFILT_VNODE,
            EV_ADD | EV_ENABLE | EV_ONESHOT,
            inotify_flags_to_kqueue (flags, 0),
            0,
            index);

    return 0;
}


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
        ws->allocated =+ (count + WS_RESERVED);
        ws->events = realloc (ws->events, sizeof (struct kevent) * ws->allocated);
        ws->watches = realloc (ws->watches, sizeof (struct watch) * ws->allocated);
        // TODO: check realloc fails
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
