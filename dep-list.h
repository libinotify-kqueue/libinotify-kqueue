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

#ifndef __DEP_LIST_H__
#define __DEP_LIST_H__

#include <sys/types.h> /* ino_t */

typedef struct dep_list {
    struct dep_list *next;

    char *path;
    ino_t inode;
} dep_list;

dep_list* dl_create       (char *path, ino_t inode);
void      dl_print        (dep_list *dl);
dep_list* dl_shallow_copy (dep_list *dl);
void      dl_shallow_free (dep_list *dl);
void      dl_free         (dep_list *dl);
dep_list* dl_listing      (const char *path);
void      dl_diff         (dep_list **before, dep_list **after);


#endif /* __DEP_LIST_H__ */
