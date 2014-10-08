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

#ifndef __WATCH_H__
#define __WATCH_H__

#include <stdint.h>    /* uint32_t */
#include <dirent.h>    /* ino_t */

typedef struct watch watch;

#include "inotify-watch.h"

#define WF_ISSUBWATCH (1<<0)  /* a type of watch */
#define WF_ISDIR      (1<<1)  /* watched item is a directory */

typedef enum watch_type {
    WATCH_USER,
    WATCH_DEPENDENCY,
} watch_type_t;


struct watch {
    i_watch *iw;              /* A pointer to parent inotify watch */
    uint32_t flags;           /* A watch flags. Not in inotify/kqueue format */
    size_t refcount;          /* number of dependency list items corresponding
                               * to that watch */ 
    int fd;                   /* file descriptor of a watched entry */
    ino_t inode;              /* inode number for the watched entry */
};

int    watch_open (int dirfd, const char *path, uint32_t flags);
watch *watch_init (i_watch *iw, watch_type_t watch_type, int fd);
void   watch_free (watch *w);

int    watch_register_event (watch *w, uint32_t fflags);

#endif /* __WATCH_H__ */
