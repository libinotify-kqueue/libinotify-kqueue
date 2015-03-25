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

#include "compat.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <sys/types.h>
#include <sys/stat.h>  /* ino_t */

#include "watch-set.h"
#include "watch.h"

/**
 * Initialize the worker sets.
 *
 * @param[in] ws A pointer to the worker sets.
 **/
void
worker_sets_init (worker_sets *ws)
{
    assert (ws != NULL);

    RB_INIT (ws);
}

/**
 * Free the memory allocated for the worker sets.
 *
 * @param[in] ws A pointer the the worker sets.
 **/
void
worker_sets_free (worker_sets *ws)
{
    assert (ws != NULL);

    watch *w, *tmp;

    RB_FOREACH_SAFE (w, worker_sets, ws, tmp) {
        worker_sets_delete (ws, w);
    }
}

/**
 * Remove a watch from worker sets.
 *
 * @param[in] ws A pointer to the worker sets.
 * @param[in] w  A pointer to watch to remove.
 **/
void
worker_sets_delete (worker_sets *ws, watch *w)
{
    assert (ws != NULL);
    assert (w != NULL);

    RB_REMOVE (worker_sets, ws, w);
    watch_free (w);
}

/**
 * Insert watch into worker sets.
 *
 * @param[in] ws A pointer to #worker_sets.
 * @param[in] w  A pointer to inserted watch.
 **/
void
worker_sets_insert (worker_sets *ws, watch *w)
{
    assert (ws != NULL);
    assert (w != NULL);

    RB_INSERT (worker_sets, ws, w);
}

/**
 * Find kqueue watch corresponding for dependency item
 *
 * @param[in] ws    A pointer to #worker_sets.
 * @param[in] inode A inode number of watch
 * @return A pointer to kqueue watch if found NULL otherwise
 **/
watch *
worker_sets_find (worker_sets *ws, ino_t inode)
{
    assert (ws != NULL);

    watch find = { .inode = inode };
    return RB_FIND (worker_sets, ws, &find);
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
worker_sets_cmp (watch *w1, watch *w2)
{
    return ((w1->inode > w2->inode) - (w1->inode < w2->inode));
}

RB_GENERATE(worker_sets, watch, link, worker_sets_cmp);
