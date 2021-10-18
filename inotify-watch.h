/*******************************************************************************
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

#ifndef __INOTIFY_WATCH_H__
#define __INOTIFY_WATCH_H__

#include "compat.h"

typedef struct i_watch i_watch;

#include "dep-list.h"
#include "watch-set.h"
#include "watch.h"
#include "worker.h"

struct i_watch {
    int wd;                    /* watch descriptor */
    int fd;                    /* file descriptor of parent kqueue watch */
    worker *wrk;               /* pointer to a parent worker structure */
    int is_closed;             /* inotify watch is stopped but not freed yet */
#ifdef SKIP_SUBFILES
    int skip_subfiles;         /* Fs is not safe to start subwatches */
#endif
    uint32_t flags;            /* flags in the inotify format */
    mode_t mode;               /* File status of the watched inode */
    ino_t inode;               /* inode number of watched inode */
    dev_t dev;                 /* device number of watched inode */
    dep_list deps;             /* dependence list of inotify watch */
    watch_set watches;         /* kqueue watches of inotify watch */
    SLIST_ENTRY(i_watch) next; /* pointer to the next inotify watch in list */
};

int      iwatch_open (const char *path, uint32_t flags);
i_watch *iwatch_init (worker *wrk, int fd, uint32_t flags);
void     iwatch_free (i_watch *iw);

void     iwatch_update_flags    (i_watch *iw, uint32_t flags);

watch*   iwatch_add_subwatch    (i_watch *iw, dep_item *di);
void     iwatch_del_subwatch    (i_watch *iw, const dep_item *di);
void     iwatch_move_subwatch   (i_watch *iw,
                                 const dep_item *di_from,
                                 const dep_item *di_to);

#endif /* __INOTIFY_WATCH_H__ */
