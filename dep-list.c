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
    dep_node *dn;

    DL_FOREACH (dn, dl) {
        printf ("%lld:%s ", (long long int) dn->item->inode, dn->item->path);
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
 * Create a new list node.
 *
 * Create a new list node and initialize its fields.
 *
 * @param[in] di Parent directory of depedence list.
 * @return A pointer to a new list or NULL in the case of error.
 **/
dep_node*
dn_create (dep_item *di)
{
    dep_node *dn = calloc (1, sizeof (dep_node));
    if (dn == NULL) {
        perror_msg ("Failed to allocate new dep-list node");
        return NULL;
    }
    dn->item = di;
    return dn;
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
 * @return A pointer to a new node or NULL in the case of error.
 **/
dep_node*
dl_insert (dep_list* dl, dep_item* di)
{
    dep_node *dn = dn_create (di);
    if (dn == NULL) {
        perror_msg ("Failed to create a new dep-list node");
        return NULL;
    }

    SLIST_INSERT_HEAD (&dl->head, dn, next);

    return dn;
}

/**
 * Remove item after specified from a list.
 *
 * @param[in] dl      A pointer to a list.
 * @param[in] prev_dn A pointer to a list node prepending removed one.
 *     Should be NULL to remove first node from a list.
 **/
void
dl_remove_after (dep_list* dl, dep_node* prev_dn)
{
    dep_node *dn;

    if (prev_dn) {
        dn = SLIST_NEXT (prev_dn, next);
        SLIST_REMOVE_AFTER (prev_dn, next);
    } else {
        dn = SLIST_FIRST (&dl->head);
        SLIST_REMOVE_HEAD (&dl->head, next);
    }

    free (dn);
}

/**
 * Create a shallow copy of a list.
 *
 * A shallow copy is a copy of a structure, but not the copy of the
 * contents. All data pointers (`item' in our case) of a list and its
 * shallow copy will point to the same memory.
 *
 * @param[in] dl A pointer to list to make a copy. May be NULL.
 * @return A shallow copy of the list.
 **/ 
dep_list*
dl_shallow_copy (const dep_list *dl)
{
    assert (dl != NULL);

    dep_list *head = dl_create ();
    if (head == NULL) {
        perror_msg ("Failed to allocate head during shallow copy");
        return NULL;
    }

    dep_node *cp = NULL;
    dep_node *iter;

    DL_FOREACH (iter, dl) {
        dep_node *dn = dn_create (iter->item);
        if (dn == NULL) {
                perror_msg ("Failed to allocate a new element during shallow copy");
                dl_shallow_free (head);
                return NULL;
        }

        if (cp == NULL) {
            SLIST_INSERT_HEAD (&head->head, dn, next);
        } else {
            SLIST_INSERT_AFTER (cp, dn, next);
        }
        cp = dn;
    }

    return head;
}

/**
 * Free the memory allocated for shallow copy.
 *
 * This function will free the memory used by a list structure, but
 * the list data will remain in the heap.
 *
 * @param[in] dl A pointer to a list. May be NULL.
 **/
void
dl_shallow_free (dep_list *dl)
{
    assert (dl != NULL);

    dep_node *dn;

    while (!SLIST_EMPTY (&dl->head)) {
        dn = SLIST_FIRST (&dl->head);
        SLIST_REMOVE_HEAD (&dl->head, next);
        free (dn);
    }

    free (dl);
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

    dep_node *dn;

    while (!SLIST_EMPTY (&dl->head)) {
        dn = SLIST_FIRST (&dl->head);
        SLIST_REMOVE_HEAD (&dl->head, next);
        di_free (dn->item);
        free (dn);
    }

    free (dl);
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

    dep_node *dn;
    DL_FOREACH (dn, dl) {
        dn->item->type &= S_IFMT;
    }
}

/*
 * Find dependency list item by filename.
 *
 * @param[in] dl    A pointer to a list.
 * @param[in] path  A name of a file.
 * @return A pointer to a dep_node if node is found, NULL otherwise.
 */
dep_node*
dl_find (dep_list *dl, const char *path)
{
    assert (dl != NULL);
    assert (path != NULL);

    dep_node *node;

    DL_FOREACH (node, dl) {
        if (strcmp (node->item->path, path) == 0) {
            return node;
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
    dep_item *item;
    dep_node *node;
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
        if (before != NULL) {
            node = dl_find (before, ent->d_name);
            if (node != NULL && node->item->inode == ent->d_ino) {
                node->item->type |= DI_UNCHANGED;
                continue;
            }
        }

        item = di_create (ent->d_name, ent->d_ino, type);
        if (item == NULL) {
            perror_msg ("Failed to allocate a new item during listing");
            goto error;
        }

        node = dl_insert (head, item);
        if (node == NULL) {
            di_free (item);
            perror_msg ("Failed to allocate a new node during listing");
            goto error;
        }
    }
    return head;

error:
    if (before != NULL) {
        dl_clearflags (before);
    }
    dl_free (head);
    return NULL;
}

/**
 * Traverses two lists. Compares items with a supplied expression
 * and performs the passed code on a match. Removes the matched entries
 * from the both lists.
 **/
#define EXCLUDE_SIMILAR(removed_list, added_list, match_expr, matched_code) \
    dep_node *removed_list##_iter, *tmp;                                \
    dep_node *removed_list##_prev = NULL;                               \
                                                                        \
    int productive = 0;                                                 \
                                                                        \
    DL_FOREACH_SAFE (removed_list##_iter, removed_list, tmp) {          \
        dep_node *added_list##_iter;                                    \
        dep_node *added_list##_prev = NULL;                             \
                                                                        \
        if (removed_list##_iter->item->type & DI_UNCHANGED) {           \
            removed_list##_prev = removed_list##_iter;                  \
            continue;                                                   \
        }                                                               \
                                                                        \
        int matched = 0;                                                \
        DL_FOREACH (added_list##_iter, added_list) {                    \
            if (match_expr) {                                           \
                matched = 1;                                            \
                ++productive;                                           \
                matched_code;                                           \
                                                                        \
                di_free (removed_list##_iter->item);                    \
                dl_remove_after (removed_list, removed_list##_prev);    \
                dl_remove_after (added_list, added_list##_prev);        \
                break;                                                  \
            }                                                           \
            added_list##_prev = added_list##_iter;                      \
        }                                                               \
        if (matched == 0) {                                             \
            removed_list##_prev = removed_list##_iter;                  \
        }                                                               \
    }                                                                   \
    return (productive > 0);


#define cb_invoke(cbs, name, udata, ...) \
    do { \
        if (cbs->name) { \
            (cbs->name) (udata, ## __VA_ARGS__); \
        } \
    } while (0)

/**
 * Detect and notify about moves in the watched directory.
 *
 * A move is what happens when you rename a file in a directory, and
 * a new name is unique, i.e. you didnt overwrite any existing files
 * with this one.
 *
 * @param[in] removed  A list of the removed files in the directory.
 * @param[in] added    A list of the added files of the directory.
 * @param[in] cbs      A pointer to #traverse_cbs, an user-defined set of
 *     traverse callbacks.
 * @param[in] udata    A pointer to the user-defined data.
 * @return 0 if no files were renamed, >0 otherwise.
**/
static int
dl_detect_moves (dep_list            *removed, 
                 dep_list            *added, 
                 const traverse_cbs  *cbs, 
                 void                *udata)
{
    assert (cbs != NULL);

     EXCLUDE_SIMILAR
        (removed, added,
         (removed_iter->item->inode == added_iter->item->inode),
         {
             cb_invoke (cbs, moved, udata, removed_iter->item, added_iter->item);
         });
}

/**
 * Detect and notify about replacements in the watched directory.
 *
 * Consider you are watching a directory foo with the folloing files
 * insinde:
 *
 *    foo/bar
 *    foo/baz
 *
 * A replacement in a watched directory is what happens when you invoke
 *
 *    mv /foo/bar /foo/bar
 *
 * i.e. when you replace a file in a watched directory with another file
 * from the same directory.
 *
 * @param[in] removed  A list of the removed files in the directory.
 * @param[in] current  A list with the current contents of the directory.
 * @param[in] cbs      A pointer to #traverse_cbs, an user-defined set of
 *     traverse callbacks.
 * @param[in] udata    A pointer to the user-defined data.
 * @return 0 if no files were renamed, >0 otherwise.
 **/
static int
dl_detect_replacements (dep_list            *removed,
                        dep_list            *current,
                        const traverse_cbs  *cbs,
                        void                *udata)
{
    assert (cbs != NULL);

    EXCLUDE_SIMILAR
        (removed, current,
         (strcmp (removed_iter->item->path, current_iter->item->path) == 0
          && removed_iter->item->inode != current_iter->item->inode),
         {
            cb_invoke (cbs, replaced, udata, removed_iter->item);
         });
}

/**
 * Detect and notify about overwrites in the watched directory.
 *
 * Consider you are watching a directory foo with a file inside:
 *
 *    foo/bar
 *
 * And you also have a directory tmp with a file 1:
 * 
 *    tmp/1
 *
 * You do not watching directory tmp.
 *
 * An overwrite in a watched directory is what happens when you invoke
 *
 *    mv /tmp/1 /foo/bar
 *
 * i.e. when you overwrite a file in a watched directory with another file
 * from the another directory.
 *
 * @param[in] previous A list with the previous contents of the directory.
 * @param[in] current  A list with the current contents of the directory.
 * @param[in] cbs      A pointer to #traverse_cbs, an user-defined set of
 *     traverse callbacks.
 * @param[in] udata    A pointer to the user-defined data.
 * @return 0 if no files were renamed, >0 otherwise.
 **/
static int
dl_detect_overwrites (dep_list            *previous,
                      dep_list            *current,
                      const traverse_cbs  *cbs,
                      void                *udata)
{
    assert (cbs != NULL);

    EXCLUDE_SIMILAR
        (previous, current,
         (strcmp (previous_iter->item->path, current_iter->item->path) == 0
          && previous_iter->item->inode != current_iter->item->inode),
         {
             cb_invoke (cbs, overwritten, udata, previous_iter->item, current_iter->item);
         });
}


/**
 * Traverse a list and invoke a callback for each item.
 * 
 * @param[in] list  A #dep_list.
 * @param[in] cb    A #single_entry_cb callback function.
 * @param[in] udata A pointer to the user-defined data.
 **/
static void 
dl_emit_single_cb_on (dep_list        *list,
                      single_entry_cb  cb,
                      void            *udata)
{
    dep_node *iter;

    if (cb == NULL)
        return;

    DL_FOREACH (iter, list) {
        if (!(iter->item->type & DI_UNCHANGED)) {
            (cb) (udata, iter->item);
        }
    }
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
 * @return 0 on success, -1 otherwise.
 **/
int
dl_calculate (dep_list           *before,
              dep_list           *after,
              const traverse_cbs *cbs,
              void               *udata)
{
    assert (before != NULL);
    assert (after != NULL);
    assert (cbs != NULL);

    int need_update = 0;
    dep_node *dn, *tmp;

    dep_list *now = dl_shallow_copy (after);
    if (now == NULL) {
        dl_clearflags (before);
        return -1;
    }

    dep_list *lst = dl_shallow_copy (now);

    /*
     * at this point dl_calculate cannot be undone on dl_shallow_copy failure
     * as some before list items has been already deleted. Handle this with
     * skipping replacements detection routines. It not so bad as we continue
     * to invoke handle_removed callback on this items instead of
     * handle_replacements
     */
    if (lst != NULL) { /* TODO: Check & verify the behavior */
        need_update += dl_detect_moves (before, now, cbs, udata);
        dl_detect_overwrites (before, now, cbs, udata);
        need_update += dl_detect_replacements (before, lst, cbs, udata);
        dl_shallow_free (lst);
    }

    if (need_update) {
        cb_invoke (cbs, names_updated, udata);
    }

    dl_emit_single_cb_on (before, cbs->removed, udata);
    dl_emit_single_cb_on (now, cbs->added, udata);

    cb_invoke (cbs, many_added, udata, now);
    cb_invoke (cbs, many_removed, udata, before);
    
    dl_shallow_free (now);

    /* Move unchanged items from before list to after list */
    DL_FOREACH_SAFE (dn, before, tmp) {
        if (dn->item->type & DI_UNCHANGED) {
            SLIST_REMOVE (&before->head, dn, dep_node, next);
            SLIST_INSERT_HEAD (&after->head, dn, next);
        }
    }
    dl_clearflags (after);
    dl_free (before);

    return 0;
}

