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

#include <stdlib.h>  /* calloc */
#include <stdio.h>   /* printf */
#include <dirent.h>  /* opendir, readdir, closedir */
#include <string.h>  /* strcmp */
#include <assert.h>

#include "utils.h"
#include "dep-list.h"

/**
 * Print a list to stdout.
 *
 * @param[in] dl A pointer to a list.
 **/
void
dl_print (dep_list *dl)
{
    while (dl != NULL) {
        printf ("%lld:%s ", dl->inode, dl->path);
        dl = dl->next;
    }
    printf ("\n");
}

/**
 * Create a new list item.
 *
 * Create a new list item and initialize its fields.
 *
 * @param[in] path  A name of a file (the string is not copied!).
 * @param[in] inode A file's inode number.
 * @return A pointer to a new item or NULL in the case of error.
 **/
dep_list* dl_create (char *path, ino_t inode)
{
    dep_list *dl = calloc (1, sizeof (dep_list));
    if (dl == NULL) {
        perror_msg ("Failed to create a new dep-list item");
        return NULL;
    }

    dl->path = path;
    dl->inode = inode;
    return dl;
}

/**
 * Create a shallow copy of a list.
 *
 * A shallow copy is a copy of a structure, but not the copy of the
 * contents. All data pointers (`path' in our case) of a list and its
 * shallow copy will point to the same memory.
 *
 * @param[in] dl A pointer to list to make a copy. May be NULL.
 * @return A shallow copy of the list.
 **/ 
dep_list*
dl_shallow_copy (dep_list *dl)
{
    if (dl == NULL) {
        return NULL;
    }

    dep_list *head = calloc (1, sizeof (dep_list));
    if (head == NULL) {
        perror_msg ("Failed to allocate head during shallow copy");
        return NULL;
    }

    dep_list *cp = head;
    dep_list *it = dl;

    while (it != NULL) {
        cp->path = it->path;
        cp->inode = it->inode;
        if (it->next) {
            cp->next = calloc (1, sizeof (dep_list));
            if (cp->next == NULL) {
                perror_msg ("Failed to allocate a new element during shallow copy");
                dl_shallow_free (head);
                return NULL;
            }
            cp = cp->next;
        }
        it = it->next;
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
    while (dl != NULL) {
        dep_list *ptr = dl;
        dl = dl->next;
        free (ptr);
    }
}

/**
 * Free the memory allocated for a list.
 *
 * This function will free all the memory used by a list: both
 * list structure and the list data.
 *
 * @param[in] dl A pointer to a list. May be NULL.
 **/
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

/**
 * Create a directory listing and return it as a list.
 *
 * @param[in] path A path to a directory.
 * @return A pointer to a list. May return NULL, check errno in this case.
 **/
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

            if (head == NULL) {
                head = calloc (1, sizeof (dep_list));
                if (head == NULL) {
                    perror_msg ("Failed to allocate head during listing");
                    goto error;
                }
            }

            dep_list *iter = (prev == NULL) ? head : calloc (1, sizeof (dep_list));
            if (iter == NULL) {
                perror_msg ("Failed to allocate a new element during listing");
                goto error;
            }

            iter->path = strdup (ent->d_name);
            if (iter->path == NULL) {
                perror_msg ("Failed to copy a string during listing");
                goto error;
            }

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

error:
    if (dir != NULL) {
        closedir (dir);
    }
    dl_free (head);
    return NULL;
}

/**
 * Perform a diff on lists.
 *
 * This function performs something like a set intersection. The same items
 * will be removed from the both lists. Items are comapred by a filename.
 * 
 * @param[in,out] before A pointer to a pointer to a list. Will contain items
 *     which were not found in the `after' list.
 * @param[in,out] after  A pointer to a pointer to a list. Will containt items
 *     which were not found in the `before' list.
 **/
void
dl_diff (dep_list **before, dep_list **after)
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
                free (after_iter);
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
            free (oldptr);
        }
    }
}
