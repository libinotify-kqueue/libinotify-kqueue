#include <stdlib.h>  /* calloc */
#include <stdio.h>   /* pritnf */
#include <dirent.h>  /* opendir, readdir, closedir */
#include <string.h>  /* strcmp */
#include <assert.h>

#include "dep-list.h"

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
    if (dl == NULL) {
        return NULL;
    }

    dep_list *head = calloc (1, sizeof (dep_list)); // TODO: check allocation
    dep_list *cp = head;
    dep_list *it = dl;

    while (it != NULL) {
        /* cp->fd = it->fd; */
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

    dep_list *head = NULL;
    dep_list *prev = NULL;
    DIR *dir = opendir (path);
    if (dir != NULL) {
        struct dirent *ent;

        while ((ent = readdir (dir)) != NULL) {
            if (!strcmp (ent->d_name, ".") || !strcmp (ent->d_name, "..")) {
                continue;
            }

            // TODO: check allocation
            if (head == NULL) {
                head = calloc (1, sizeof (dep_list)); // TODO: check allocation
            }

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

    if (*before == NULL || *after == NULL) {
        return;
    }

    dep_list *before_iter = *before;
    dep_list *before_prev = NULL;

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
