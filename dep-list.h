#ifndef __DEP_LIST_H__
#define __DEP_LIST_H__

#include <sys/types.h> /* ino_t */

typedef struct dep_list {
    struct dep_list *next;

    char *path;
    ino_t inode;
} dep_list;

void      dl_print        (dep_list *dl);
dep_list* dl_shallow_copy (dep_list *dl);
void      dl_shallow_free (dep_list *dl);
void      dl_free         (dep_list *dl);
dep_list* dl_listing      (const char *path);
void      dl_diff         (dep_list **before, dep_list **after);


#endif /* __DEP_LIST_H__ */
