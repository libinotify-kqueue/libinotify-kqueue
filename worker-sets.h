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

#ifndef __WORKER_SETS_H__
#define __WORKER_SETS_H__

#include <stdint.h>
#include <sys/types.h> /* size_t */
#include <sys/stat.h>  /* ino_t */

#include "watch.h"

typedef struct worker_sets {
    struct watch **watches;   /* appropriate watches with additional info */
    size_t length;            /* size of active entries */
    size_t allocated;         /* size of allocated entries */
} worker_sets;

int  worker_sets_init   (worker_sets *ws);
void worker_sets_free   (worker_sets *ws);
void worker_sets_delete (worker_sets *ws, size_t index);
int  worker_sets_insert (worker_sets *ws, watch *w);
watch *worker_sets_find (worker_sets *ws, ino_t inode);


#endif /* __WORKER_SETS_H__ */
