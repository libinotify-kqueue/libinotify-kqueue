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

#ifndef __WATCH_H__
#define __WATCH_H__

#include "compat.h"

#include <assert.h>    /* assert */
#include <dirent.h>    /* ino_t */
#include <stdbool.h>

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>  /* stat */

typedef struct watch watch;
/* Inherit watch_flags_t from <sys/stat.h> mode_t type.
 * It is hackish but allow to use existing stat macroses */
typedef mode_t watch_flags_t;

#include "inotify-watch.h"

#define WF_ISSUBWATCH S_IXOTH /* a type of watch */
#define WF_DELETED    S_IROTH /* file`s link count == 0 */
#define WF_SKIP_NEXT  S_IWOTH /* Some evens (open/close/read) should be skipped
                               * on the next round as produced by libinotify */

#define WD_FOREACH(wd, w) SLIST_FOREACH ((wd), &(w)->deps, next)

typedef enum watch_type {
    WATCH_USER,
    WATCH_DEPENDENCY,
} watch_type_t;

SLIST_HEAD(watch_dep_list, watch_dep);
struct watch_dep {
    const dep_item *di;
    SLIST_ENTRY(watch_dep) next;
};

struct watch {
    i_watch *iw;              /* A pointer to parent inotify watch */
    watch_flags_t flags;      /* A watch flags. Not in inotify/kqueue format */
    int fd;                   /* file descriptor of a watched entry */
    ino_t inode;              /* inode number taken from readdir call */
    struct watch_dep_list deps; /* An associated dep_items list */
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

struct watch_dep *watch_find_dep (watch *w, const dep_item *di);
struct watch_dep *watch_add_dep  (watch *w, const dep_item *di);
struct watch_dep *watch_del_dep  (watch *w, const dep_item *di);
struct watch_dep *watch_chg_dep  (watch *w,
                                  const dep_item *di_from,
                                  const dep_item *di_to);

int    watch_register_event (watch *w, uint32_t fflags);

/**
 * Checks if #watch is associated with any file dependency or not.
 *
 * @param[in] w A pointer to the #watch.
 * @return true if A #watch has associated dependency records. false otherwise.
 **/
static inline bool
watch_deps_empty (watch *w)
{
    assert (w != NULL);
    return (SLIST_EMPTY (&w->deps));
}

#endif /* __WATCH_H__ */
