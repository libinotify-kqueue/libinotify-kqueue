/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#ifndef __DEP_LIST_H__
#define __DEP_LIST_H__

#include "compat.h"

#include <sys/types.h> /* ino_t */
#include <sys/stat.h>  /* mode_t */

#define DI_UNCHANGED S_IXOTH /* dep_item remained unchanged between listings */
#define DI_REPLACED  S_IROTH /* dep_item was replaced by other item */
#define DI_READDED   DI_REPLACED /* dep_item replaced other item */
#define DI_MOVED     S_IWOTH /* dep_item was renamed between listings */
#define DI_EXT_PATH  S_IRWXO /* special dep_item intended for search only */

#define S_IFUNK 0000000 /* mode_t extension. File type is unknown */
#define S_ISUNK(m) (((m) & S_IFMT) == S_IFUNK)

#define CL_FOREACH(di, dl) \
    SLIST_FOREACH ((di), &(dl)->head, list_link)
#define DL_FOREACH(di, dl) \
    RB_FOREACH ((di), dep_tree, &(dl)->head)
#define DL_FOREACH_SAFE(di, dl, tmp_di) \
    RB_FOREACH_SAFE ((di), dep_tree, &(dl)->head, (tmp_di))

typedef struct dep_item {
    union {
        RB_ENTRY(dep_item) tree_link;
        struct {
            SLIST_ENTRY(dep_item) list_link;
            struct dep_item *replacee;
            struct dep_item *moved_from;
        };
        const char *ext_path;
    };
    ino_t inode;
    mode_t type;
    char path[];
} dep_item;

typedef struct dep_list {
    RB_HEAD(dep_tree, dep_item) head;
} dep_list;

typedef struct chg_list {
    SLIST_HEAD(, dep_item) head;
} chg_list;

typedef void (* single_entry_cb) (void *udata, dep_item *di);
typedef void (* dual_entry_cb)   (void *udata,
                                  dep_item *from_di,
                                  dep_item *to_di);

typedef struct traverse_cbs {
    single_entry_cb  added;
    single_entry_cb  removed;
    single_entry_cb  replaced;
    dual_entry_cb    moved;
} traverse_cbs;

dep_item* di_create       (const char *path, ino_t inode, mode_t type);
void      di_free         (dep_item *di);
dep_list* dl_alloc        ();
void      dl_init         (dep_list *dl);
dep_list* dl_create       ();
void      dl_insert       (dep_list *dl, dep_item *di);
void      dl_remove       (dep_list *dl, dep_item *di);
void      dl_print        (dep_list *dl);
void      dl_free         (dep_list *dl);
void      dl_join         (dep_list *dl_target, chg_list *dl_source);
dep_item* dl_find         (dep_list *dl, const char *path);
chg_list* dl_readdir      (DIR *dir, dep_list *before);

void
dl_calculate (dep_list            *before,
              chg_list            *after,
              const traverse_cbs  *cbs,
              void                *udata);

static inline void
di_settype (dep_item *di, mode_t type)
{
    di->type = (di->type & ~S_IFMT) | (type & S_IFMT);
}

RB_PROTOTYPE(dep_tree, dep_item, tree_link, dep_item_cmp);

#endif /* __DEP_LIST_H__ */
