/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
  SPDX-License-Identifier: MIT

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
#include <dirent.h>  /* opendir, readdir, closedir */
#include <errno.h>   /* errno */
#include <fcntl.h>   /* open */
#include <stddef.h>  /* offsetof */
#include <stdlib.h>  /* calloc */
#include <string.h>  /* strcmp */
#include <unistd.h>  /* close */

#include "compat.h"
#include "config.h"
#include "dep-list.h"
#include "utils.h"

static inline void di_free (struct dep_item *di);
static int dep_item_cmp (struct dep_item *di1, struct dep_item *di2);

RB_GENERATE_INSERT_COLOR(dep_list, dep_item, u.tree_link, static)
RB_GENERATE_REMOVE_COLOR(dep_list, dep_item, u.tree_link, static)
RB_GENERATE_INSERT(dep_list, dep_item, u.tree_link, dep_item_cmp, static)
RB_GENERATE_REMOVE(dep_list, dep_item, u.tree_link, static)
RB_GENERATE_FIND(dep_list, dep_item, u.tree_link, dep_item_cmp, static)

/**
 * Initialize a rb-tree based list.
 *
 * @param[in] dl A pointer to a list.
 **/
void
dl_init (struct dep_list* dl)
{
    assert (dl != NULL);
    RB_INIT (dl);
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
static inline struct dep_item*
di_create (const char *path, ino_t inode, mode_t type)
{
    size_t pathlen = strlen (path) + 1;

    struct dep_item *di = calloc (1, offsetof (struct dep_item, path) + pathlen);
    if (di == NULL) {
        perror_msg (("Failed to create a new dep-list item"));
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
static inline void
dl_insert (struct dep_list* dl, struct dep_item* di)
{
    assert (dl != NULL);
    assert (di != NULL);
    assert (RB_FIND (dep_list, dl, di) == NULL);

    RB_INSERT (dep_list, dl, di);
}

/**
 * Remove specified item from a list.
 *
 * @param[in] dl A pointer to a list.
 * @param[in] di A pointer to a list item to remove.
 **/
static inline void
dl_remove (struct dep_list* dl, struct dep_item* di)
{
    assert (dl != NULL);
    assert (di != NULL);
    assert (RB_FIND (dep_list, dl, di) != NULL);

    RB_REMOVE (dep_list, dl, di);
    di_free (di);
}

/**
 * Free the memory allocated for a list item.
 *
 * This function will free the memory used by a list item.
 *
 * @param[in] dn A pointer to a list item. May be NULL.
 **/
static inline void
di_free (struct dep_item *di)
{
    free (di);
}

/**
 * Free the memory allocated for a list.
 *
 * This function will rmove and free all list items
 *
 * @param[in] dl A pointer to a list.
 **/
void
dl_free (struct dep_list *dl)
{
    struct dep_item *di;

    assert (dl != NULL);

    while (!RB_EMPTY (dl)) {
        di = RB_MIN (dep_list, dl);
        dl_remove (dl, di);
    }
}

/**
 * Merge linked list based source directory listing into
 * target directory listing.
 *
 * This function will free all the memory used by a source list: both
 * list structure and the list data.
 *
 * @param[in] dl_target A pointer to a target list.
 * @param[in] dl_source A pointer to a source list (linked list based).
 **/
void
dl_join (struct dep_list *dl_target, struct chg_list *dl_source)
{
    struct dep_item *di;

    assert (dl_target != NULL);
    assert (dl_source != NULL);

    while (!SLIST_EMPTY (dl_source)) {
        di = SLIST_FIRST (dl_source);
        SLIST_REMOVE_HEAD (dl_source, u.s.list_link);
        dl_insert (dl_target, di);
    }
    free (dl_source);
}

/**
 * Reset flags of all list items.
 *
 * @param[in] dl A pointer to a list.
 **/
static inline void
dl_clearflags (struct dep_list *dl)
{
    struct dep_item *di;

    assert (dl != NULL);

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
struct dep_item*
dl_find (struct dep_list *dl, const char *path)
{
    struct dep_item find;

    assert (dl != NULL);
    assert (path != NULL);

    find.type = DI_EXT_PATH;
    find.u.ext_path = path;

    return (RB_FIND (dep_list, dl, &find));
}

/**
 * Create a directory listing from DIR stream and return it as a linked list.
 *
 * @param[in] dir    A pointer to valid directory stream created with opendir().
 * @param[in] before A pointer to previous directory listing. If nonNULL value
 *                   is specified, unchanged entries are not included in
 *                   resulting list but marked as unchanged in before list.
 * @return A pointer to a list. May return NULL, check errno in this case.
 **/
struct chg_list*
dl_readdir (DIR *dir, struct dep_list* before)
{
    struct dirent *ent;
    struct dep_item *item, *before_item;
    struct chg_list *head;
    mode_t type;

    assert (dir != NULL);

    head = calloc (1, sizeof (struct dep_list));
    if (head == NULL) {
        perror_msg (("Failed to allocate list during directory listing"));
        return NULL;
    }
    SLIST_INIT (head);

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
            perror_msg (("Failed to allocate a new item during listing"));
            goto error;
        }

        /* File was overwritten between scans. Cache reference on old entry. */
        if (before_item != NULL) {
            item->type |= DI_READDED;
            item->u.s.replacee = before_item;
        }

        SLIST_INSERT_HEAD (head, item, u.s.list_link);
    }
    return head;

error:
    if (before != NULL) {
        dl_clearflags (before);
    }
    while (!SLIST_EMPTY (head)) {
        item = SLIST_FIRST (head);
        SLIST_REMOVE_HEAD (head, u.s.list_link);
        di_free (item);
    }
    free (head);
    return NULL;
}

/**
 * Create a directory listing and return it as a list.
 *
 * @return A pointer to a list. May return NULL, check errno in this case.
 **/
struct chg_list*
dl_listing (int fd, struct dep_list* before)
{
    DIR *dir = NULL;
    struct chg_list *head;

    assert (fd >= 0);

    dir = fdreopendir (fd);
    if (dir == NULL) {
        if (errno == ENOENT) {
            /* ENOENT is skipped as the directory could be just deleted */
            head = calloc (1, sizeof (struct chg_list));
            if (head != NULL) {
                SLIST_INIT (head);
                return (head);
            }
            perror_msg (("Failed to allocate list during directory listing"));
        }
        perror_msg (("Failed to opendir for listing"));
        return NULL;
    }

    head = dl_readdir (dir, before);

#if READDIR_DOES_OPENDIR > 0
    closedir (dir);
#else
    fdclosedir (dir);
#endif

    return head;
}


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
dl_calculate (struct dep_list           *before,
              struct chg_list           *after,
              const struct traverse_cbs *cbs,
              void                      *udata)
{
    struct dep_item *di_from, *di_to, *tmp;
    size_t n_moves = 0;

    assert (before != NULL);
    assert (cbs != NULL);

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
    if (after != NULL) {
        DL_FOREACH (di_from, before) {
            /* Skip unchanged files. They do not produce any events. */
            if (di_from->type & DI_UNCHANGED) {
                continue;
            }

            /* Detect and notify about moves in the watched directory. */
            CL_FOREACH (di_to, after) {
                if (di_from->inode == di_to->inode &&
                    !(di_to->type & DI_MOVED)) {
                    /* Detect replacements in the watched directory */
                    if (di_to->type & DI_READDED) {
                        di_to->u.s.replacee->type |= DI_REPLACED;
                    }

                    /* Now we can mark item as moved in the watched directory */
                    di_to->type |= DI_MOVED;
                    di_to->u.s.moved_from = di_from;
                    di_from->type |= DI_MOVED;
                    ++n_moves;
                    break;
                }
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
                cbs->replaced (udata, di_from);
            } else {
                cbs->removed (udata, di_from);
            }
        }
    }

    if (after != NULL) {
        /*
         * Notify about files that have been renamed in between scans
         *
         * Here we are doing several passes to provide ordering for overlapping
         * renames. Renames overlap if they share common filename e.g. if
         * next commands "mv file file.bak; mv file.new file;" were executed
         * in between consecutive directory scans.
         * On each round we are reporting only moves that does not replace
         * files parcitipating in other move. Than mark this file as not
         * participating in moves to allow further progress in next round.
         */
        bool want_overlap = false;
        while (n_moves > 0) {
            size_t n_moves_prev = n_moves;
            CL_FOREACH (di_to, after) {
                bool is_overlap = di_to->type & DI_READDED &&
                                  di_to->u.s.replacee->type & DI_MOVED;
                if (di_to->type & DI_MOVED && di_to->u.s.moved_from != NULL &&
                    (is_overlap == want_overlap)) {
                    cbs->moved (udata, di_to->u.s.moved_from, di_to);

                    /* Mark file as not participating in moves */
                    di_to->u.s.moved_from->type &= ~DI_MOVED;
                    di_to->u.s.moved_from = NULL;

                    want_overlap = false;
                    --n_moves;
                }
            }
            /*
             * No progress? Unbeilivable! Unfortunatelly, we cannot handle this
             * properly without adding of renames to and from temporary file.
             * So just break circular chain at random place. :-(
             */
            if (n_moves_prev == n_moves) {
                perror_msg (("Circular rename detected"));
                want_overlap = true;
            }
        }
        /* Notify about newly created files */
        CL_FOREACH (di_to, after) {
            if (!(di_to->type & DI_MOVED)) {
                cbs->added (udata, di_to);
            }
        }
    }

    /* Replace all changed items from before list with items from after list */
    DL_FOREACH_SAFE (di_from, before, tmp) {
        if (!(di_from->type & DI_UNCHANGED)) {
            dl_remove (before, di_from);
        }
    }
    if (after != NULL) {
        dl_join (before, after);
    }
    dl_clearflags (before);
}

/**
 * Custom comparison function that can compare directory dependency list
 * entries through pointers passed by RB tree functions
 *
 * @param[in] di1 A pointer to a first deplist item to compare
 * @param[in] di2 A pointer to a second deplist item to compare
 * @return An -1, 0, or +1 if the first inode is considered to be respectively
 *     less than, equal to, or greater than the second one.
 **/
static int
dep_item_cmp (struct dep_item *di1, struct dep_item *di2)
{
    const char *path1 = (di1->type == DI_EXT_PATH) ? di1->u.ext_path : di1->path;
    const char *path2 = (di2->type == DI_EXT_PATH) ? di2->u.ext_path : di2->path;

    return strcmp (path1, path2);
}
