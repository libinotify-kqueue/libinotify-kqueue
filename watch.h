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

#include "inotify-watch.h"

#define WD_FOREACH(wd, w) SLIST_FOREACH ((wd), &(w)->deps, next)

SLIST_HEAD(watch_dep_list, watch_dep);
struct watch_dep {
    struct i_watch *iw;          /* A pointer to parent inotify watch */
    const struct dep_item *di;
    SLIST_ENTRY(watch_dep) next;
};

struct watch {
    int fd;                   /* file descriptor of a watched entry */
    uint32_t fflags;          /* kqueue vnode filter flags currently applied */
    dev_t dev;                /* ID of the device containing the watch */
    ino_t inode;              /* inode number taken from readdir call */
    bool skip_next;           /* next kevent can be produced by readdir call */
    struct watch_dep_list deps; /* An associated dep_items list */
    RB_ENTRY(watch) link;     /* RB tree links */
};

uint32_t inotify_to_kqueue (uint32_t flags, mode_t mode, bool is_subwatch);
uint32_t kqueue_to_inotify (uint32_t flags,
                            mode_t mode,
                            bool is_parent,
                            bool is_deleted);

int           watch_open     (int dirfd, const char *path, uint32_t flags);
struct watch* watch_init     (int fd, struct stat *st);
void          watch_free     (struct watch *w);
mode_t        watch_get_mode (struct watch *w);

struct watch_dep *watch_find_dep (struct watch *w,
                                  struct i_watch *iw,
                                  const struct dep_item *di);
struct watch_dep *watch_add_dep  (struct watch *w,
                                  struct i_watch *iw,
                                  const struct dep_item *di);
struct watch_dep *watch_del_dep  (struct watch *w,
                                  struct i_watch *iw,
                                  const struct dep_item *di);
struct watch_dep *watch_chg_dep  (struct watch *w,
                                  struct i_watch *iw,
                                  const struct dep_item *di_from,
                                  const struct dep_item *di_to);

int    watch_register_event (struct watch *w, int kq, uint32_t fflags);
int    watch_update_event   (struct watch *w);

/**
 * Checks if #watch is associated with any file dependency or not.
 *
 * @param[in] w A pointer to the #watch.
 * @return true if A #watch has associated dependency records. false otherwise.
 **/
static inline bool
watch_deps_empty (struct watch *w)
{
    assert (w != NULL);
    return (SLIST_EMPTY (&w->deps));
}

/**
 * Checks if #watch_dep is pointing to virtual parent dependency item.
 *
 * @param[in] w A pointer to the #watch_dep.
 * @return true if A #watch_set is pointing to parent. false otherwise.
 **/
static inline bool
watch_dep_is_parent (const struct watch_dep *wd)
{
    return (wd->di == DI_PARENT);
}

/* Leave this as macro to break circular header depedency chain */
#define watch_dep_get_mode(wd) \
    (watch_dep_is_parent (wd) ? (wd)->iw->mode : (wd)->di->type)

#endif /* __WATCH_H__ */
