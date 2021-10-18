/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#include <sys/types.h>
#include <sys/stat.h>  /* ino_t */

#include <assert.h>
#include <stddef.h> /* NULL */

#include "compat.h"
#include "watch-set.h"
#include "watch.h"

static int watch_set_cmp (struct watch *w1, struct watch *w2);

RB_GENERATE_NEXT(watch_set, watch, link, static inline)
RB_GENERATE_MINMAX(watch_set, watch, link, static inline)
RB_GENERATE_INSERT_COLOR(watch_set, watch, link, static inline)
RB_GENERATE_REMOVE_COLOR(watch_set, watch, link, static inline)
RB_GENERATE_INSERT(watch_set, watch, link, watch_set_cmp, static inline)
RB_GENERATE_REMOVE(watch_set, watch, link, static inline)
RB_GENERATE_FIND(watch_set, watch, link, watch_set_cmp, static inline)

/**
 * Initialize the watch set.
 *
 * @param[in] ws A pointer to the watch set.
 **/
void
watch_set_init (struct watch_set *ws)
{
    assert (ws != NULL);

    RB_INIT (ws);
}

/**
 * Free the memory allocated for the watch set.
 *
 * @param[in] ws A pointer the the watch set.
 **/
void
watch_set_free (struct watch_set *ws)
{
    struct watch *w, *tmp;

    assert (ws != NULL);

    RB_FOREACH_SAFE (w, watch_set, ws, tmp) {
        watch_set_delete (ws, w);
    }
}

/**
 * Remove a watch from watch set.
 *
 * @param[in] ws A pointer to the watch set.
 * @param[in] w  A pointer to watch to remove.
 **/
void
watch_set_delete (struct watch_set *ws, struct watch *w)
{
    assert (ws != NULL);
    assert (w != NULL);

    RB_REMOVE (watch_set, ws, w);
    watch_free (w);
}

/**
 * Insert watch into watch set.
 *
 * @param[in] ws A pointer to #watch_set.
 * @param[in] w  A pointer to inserted watch.
 **/
void
watch_set_insert (struct watch_set *ws, struct watch *w)
{
    assert (ws != NULL);
    assert (w != NULL);

    RB_INSERT (watch_set, ws, w);
}

/**
 * Find kqueue watch corresponding for dependency item
 *
 * @param[in] ws    A pointer to #watch_set.
 * @param[in] inode A inode number of watch
 * @return A pointer to kqueue watch if found NULL otherwise
 **/
struct watch *
watch_set_find (struct watch_set *ws, dev_t dev, ino_t inode)
{
    struct watch find;

    assert (ws != NULL);

    find.dev = dev;
    find.inode = inode;
    return RB_FIND (watch_set, ws, &find);
}
/**
 * Custom comparison function that can compare kqueue watch inode values
 * through pointers passed by RB tree functions
 *
 * @param[in] w1 A pointer to a first kqueue watch to compare
 * @param[in] w2 A pointer to a second kqueue watch to compare
 * @return An -1, 0, or +1 if the first watch is considered to be respectively
 * less than, equal to, or greater than the second one.
 **/
static int
watch_set_cmp (struct watch *w1, struct watch *w2)
{
    if (w1->dev == w2->dev)
        return ((w1->inode > w2->inode) - (w1->inode < w2->inode));
    else
        return ((w1->dev > w2->dev) - (w1->dev < w2->dev));
}
