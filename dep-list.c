/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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

#include "config.h"
#include "compat.h"

#include <errno.h>   /* errno */
#include <stddef.h>  /* offsetof */
#include <stdlib.h>  /* calloc */
#include <stdio.h>   /* printf */
#include <dirent.h>  /* opendir, readdir, closedir */
#include <string.h>  /* strcmp */
#include <fcntl.h>   /* open */
#include <unistd.h>  /* close */
#include <assert.h>
#include <errno.h>

#include "utils.h"
#include "dep-list.h"

/**
 * Print a list to stdout.
 *
 * @param[in] dl A pointer to a list.
 **/
void
dl_print (const dep_list *dl)
{
    dep_item *di;

    DL_FOREACH (di, dl) {
        printf ("%lld:%s ", (long long int) di->inode, di->path);
    }
    printf ("\n");
}

/**
 * Create a new list.
 *
 * Create a new list and initialize its fields.
 *
 * @return A pointer to a new list or NULL in the case of error.
 **/
dep_list*
dl_create ()
{
    dep_list *dl = calloc (1, sizeof (dep_list));
    if (dl == NULL) {
        perror_msg ("Failed to allocate new dep-list");
        return NULL;
    }
    SLIST_INIT (&dl->head);
    return dl;
}

/**
 * Create a new list item.
 *
 * Create a new list item and initialize its fields.
 *
 * @param[in] path  A name of a file (the string is not copied!).
 * @param[in] inode A file's inode number.
 * @param[in] type  A file`s type (compatible with mode_t values)
 * @return A pointer to a new item or NULL in the case of error.
 **/
dep_item*
di_create (const char *path, ino_t inode, mode_t type)
{
    size_t pathlen = strlen (path) + 1;

    dep_item *di = calloc (1, offsetof (dep_item, path) + pathlen);
    if (di == NULL) {
        perror_msg ("Failed to create a new dep-list item");
        return NULL;
    }

    strlcpy (di->path, path, pathlen);
    di->inode = inode;
    di->type = type;
    return di;
}

/**
 * Insert new item into list.
 *
 * @param[in] dl A pointer to a list.
 * @param[in] di A pointer to a list item to be inserted.
 **/
void
dl_insert (dep_list* dl, dep_item* di)
{
    assert (dl != NULL);
    assert (di != NULL);

    SLIST_INSERT_HEAD (&dl->head, di, next);
}

/**
 * Remove item after specified from a list.
 *
 * @param[in] dl      A pointer to a list.
 * @param[in] prev_di A pointer to a list item prepending removed one.
 *     Should be NULL to remove first node from a list.
 **/
void
dl_remove_after (dep_list* dl, dep_item* prev_di)
{
    dep_item *di;

    if (prev_di) {
        di = SLIST_NEXT (prev_di, next);
        SLIST_REMOVE_AFTER (prev_di, next);
    } else {
        di = SLIST_FIRST (&dl->head);
        SLIST_REMOVE_HEAD (&dl->head, next);
    }

    di_free (di);
}

/**
 * Free the memory allocated for a list item.
 *
 * This function will free the memory used by a list item.
 *
 * @param[in] dn A pointer to a list item. May be NULL.
 **/
void
di_free (dep_item *di)
{
    free (di);
}

/**
 * Free the memory allocated for a list.
 *
 * This function will free all the memory used by a list: both
 * list structure and the list data.
 *
 * @param[in] dl A pointer to a list.
 **/
void
dl_free (dep_list *dl)
{
    assert (dl != NULL);

    dep_item *di;

    while (!SLIST_EMPTY (&dl->head)) {
        di = SLIST_FIRST (&dl->head);
        SLIST_REMOVE_HEAD (&dl->head, next);
        di_free (di);
    }

    free (dl);
}

/**
 * Merge source directory listing into target directory listing.
 *
 * This function will free all the memory used by a source list: both
 * list structure and the list data.
 *
 * @param[in] dl_target A pointer to a target list.
 * @param[in] dl_source A pointer to a source list.
 **/
void
dl_join (dep_list *dl_target, dep_list *dl_source)
{
    assert (dl_target != NULL);
    assert (dl_source != NULL);

    dep_item *di;

    while (!SLIST_EMPTY (&dl_source->head)) {
        di = SLIST_FIRST (&dl_source->head);
        SLIST_REMOVE_HEAD (&dl_source->head, next);
        dl_insert (dl_target, di);
    }
    free (dl_source);
}

/**
 * Reset flags of all list items.
 *
 * @param[in] dl A pointer to a list.
 **/
static void
dl_clearflags (dep_list *dl)
{
    assert (dl != NULL);

    dep_item *di;
    DL_FOREACH (di, dl) {
        di->type &= S_IFMT;
    }
}

/*
 * Find dependency list item by filename.
 *
 * @param[in] dl    A pointer to a list.
 * @param[in] path  A name of a file.
 * @return A pointer to a dep_item if item is found, NULL otherwise.
 */
dep_item*
dl_find (dep_list *dl, const char *path)
{
    assert (dl != NULL);
    assert (path != NULL);

    dep_item *item;

    DL_FOREACH (item, dl) {
        if (strcmp (item->path, path) == 0) {
            return item;
        }
    }

    return NULL;
}

/**
 * Create a directory listing from directory stream and return it as a list.
 *
 * @param[in] dir    A pointer to valid directory stream created with opendir().
 * @param[in] before A pointer to previous directory listing. If nonNULL value
 *                   is specified, unchanged entries are not included in
 *                   resulting list but marked as unchanged in before list.
 * @return A pointer to a list. May return NULL, check errno in this case.
 **/
dep_list*
dl_readdir (DIR *dir, dep_list* before)
{
    assert (dir != NULL);

    struct dirent *ent;
    dep_item *item, *before_item;
    mode_t type;

    dep_list *head = dl_create ();
    if (head == NULL) {
        perror_msg ("Failed to allocate list during directory listing");
        return NULL;
    }

    while ((ent = readdir (dir)) != NULL) {
        if (!strcmp (ent->d_name, ".") || !strcmp (ent->d_name, "..")) {
            continue;
        }

#ifdef HAVE_STRUCT_DIRENT_D_TYPE
        if (ent->d_type != DT_UNKNOWN)
            type = DTTOIF (ent->d_type) & S_IFMT;
        else
#endif
            type = S_IFUNK;

        /*
         * Detect files remained unmoved between directory scans.
         * This produces both intersection and symmetric diffrence of two sets.
         * The same items will be marked as UNCHANGED in previous list and
         * missed in returned set. Items are compared by name and inode number.
         */
        before_item = NULL;
        if (before != NULL) {
            before_item = dl_find (before, ent->d_name);
            if (before_item != NULL && before_item->inode == ent->d_ino) {
                before_item->type |= DI_UNCHANGED;
                continue;
            }
        }

        item = di_create (ent->d_name, ent->d_ino, type);
        if (item == NULL) {
            perror_msg ("Failed to allocate a new item during listing");
            goto error;
        }

        /* File was overwritten between scans. Cache reference on old entry. */
        if (before_item != NULL) {
            item->type |= DI_READDED;
            item->cookie = before_item;
        }

        dl_insert (head, item);
    }
    return head;

error:
    if (before != NULL) {
        dl_clearflags (before);
    }
    dl_free (head);
    return NULL;
}


#define cb_invoke(cbs, name, udata, ...) \
    do { \
        if (cbs->name) { \
            (cbs->name) (udata, ## __VA_ARGS__); \
        } \
    } while (0)


/**
 * Recognize all the changes in the directory, invoke the appropriate callbacks.
 *
 * This is the core function of directory diffing submodule.
 * It deletes before list content on successful completion.
 *
 * @param[in] before The previous contents of the directory.
 * @param[in] after  The current contents of the directory.
 * @param[in] cbs    A pointer to user callbacks (#traverse_callbacks).
 * @param[in] udata  A pointer to user data.
 **/
void
dl_calculate (dep_list           *before,
              dep_list           *after,
              const traverse_cbs *cbs,
              void               *udata)
{
    assert (before != NULL);
    assert (after != NULL);
    assert (cbs != NULL);

    dep_item *di_from, *di_to, *tmp;

    /*
     * Some terminology. Between 2 consecutive directory scans file can be:
     * unchanged - Nothing happened.
     * added     - File was created or moved in from other directory.
     * removed   - File was deleted/unlinked or moved out to other directory.
     * moved     - File name was changed inside the watched directory.
     * replaced  - File was overwritten by other file that was moved
     *             (renamed inside the watched directory).
     * readded   - File was created with the name of just deleted file or
     *             moved and then overwrote other file.
     */
    DL_FOREACH (di_from, before) {
        /* Skip unchanged files. They do not produce any events. */
        if (di_from->type & DI_UNCHANGED) {
            continue;
        }

        /* Detect and notify about moves in the watched directory. */
        DL_FOREACH (di_to, after) {
            if (di_from->inode == di_to->inode && !(di_to->type & DI_MOVED)) {
                /* Detect replacements in the watched directory */
                if (di_to->type & DI_READDED) {
                    di_to->cookie->type |= DI_REPLACED;
                }

                /* Now we can mark item as moved in the watched directory */
                di_to->type |= DI_MOVED;
                di_from->type |= DI_MOVED;
                di_to->cookie = di_from;
                break;
            }
        }
    }

    /* Traverse lists and invoke a callback for each item.
     *
     * Note about correct order of events:
     * Notification about file that disapeared (was removed or moved from)
     * from directory MUST always prepend notification about file with the
     * same name that appeared (added or moved to) in directory.
     * To obey this rule run it in next sequence:
     * 1. Notyfy about all deleted files.
     * 2. Notify about all renamed files.
     * 3. Notify about all created files.
     */
    /* Notify about files that have been deleted or replaced */
    DL_FOREACH (di_from, before) {
        if (!(di_from->type & (DI_UNCHANGED | DI_MOVED))) {
            if (di_from->type & DI_REPLACED) {
                cb_invoke (cbs, replaced, udata, di_from);
            } else {
                cb_invoke (cbs, removed, udata, di_from);
            }
        }
    }
    /* Notify about files that have been renamed in between scans */
    DL_FOREACH (di_to, after) {
        if (di_to->type & DI_MOVED) {
            cb_invoke (cbs, moved, udata, di_to->cookie, di_to);
        }
    }
    /* Notify about newly created files */
    DL_FOREACH (di_to, after) {
        if (!(di_to->type & DI_MOVED)) {
            cb_invoke (cbs, added, udata, di_to);
        }
    }

    /* Replace all changed items from before list with items from after list */
    dep_item *di_prev = NULL;
    DL_FOREACH_SAFE (di_from, before, tmp) {
        if (di_from->type & DI_UNCHANGED) {
            di_prev = di_from;
        } else {
            dl_remove_after (before, di_prev);
        }
    }
    dl_join (before, after);
    dl_clearflags (before);
}

