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

#include "compat.h"

#include <dirent.h>    /* ino_t */

#include <sys/types.h>
#include <sys/stat.h>  /* stat */

typedef struct watch watch;
/* Inherit watch_flags_t from <sys/stat.h> mode_t type.
 * It is hackish but allow to use existing stat macroses */
typedef mode_t watch_flags_t;

#include "inotify-watch.h"

#define WF_ISSUBWATCH S_IXOTH /* a type of watch */
#define WF_DELETED    S_IROTH /* file`s link count == 0 */

typedef enum watch_type {
    WATCH_USER,
    WATCH_DEPENDENCY,
} watch_type_t;


struct watch {
    i_watch *iw;              /* A pointer to parent inotify watch */
    watch_flags_t flags;      /* A watch flags. Not in inotify/kqueue format */
    size_t refcount;          /* number of dependency list items corresponding
                               * to that watch */ 
    int fd;                   /* file descriptor of a watched entry */
    ino_t inode;              /* inode number taken from readdir call */
    RB_ENTRY(watch) link;     /* RB tree links */
};

uint32_t inotify_to_kqueue (uint32_t flags, watch_flags_t wf);
uint32_t kqueue_to_inotify (uint32_t flags, watch_flags_t wf);

int    watch_open (int dirfd, const char *path, uint32_t flags);
watch *watch_init (i_watch *iw,
                   watch_type_t watch_type,
                   int fd,
                   struct stat *st);
void   watch_free (watch *w);

int    watch_register_event (watch *w, uint32_t fflags);

#endif /* __WATCH_H__ */
