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

#ifndef __WATCH_SET_H__
#define __WATCH_SET_H__

#include "compat.h"

#include <sys/types.h> /* size_t */
#include <sys/stat.h>  /* ino_t */

typedef RB_HEAD(watch_set, watch) watch_set;

#include "watch.h"

void   watch_set_init   (watch_set *ws);
void   watch_set_free   (watch_set *ws);
void   watch_set_delete (watch_set *ws, watch *w);
void   watch_set_insert (watch_set *ws, watch *w);
watch *watch_set_find   (watch_set *ws, dev_t dev, ino_t inode);

RB_PROTOTYPE(watch_set, watch, link, watch_cmp);


#endif /* __WATCH_SET_H__ */
