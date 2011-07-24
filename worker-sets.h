#ifndef __WORKER_SETS_H__
#define __WORKER_SETS_H__

#include <stdint.h>
#include <sys/types.h> /* size_t */

typedef struct dep_list {
    struct dep_list *next;

    int fd;
    char *path;
    ino_t inode;
} dep_list;

void      dl_print        (dep_list *dl);
dep_list* dl_shallow_copy (dep_list *dl);
void      dl_shallow_free (dep_list *dl);
void      dl_free         (dep_list *dl);
dep_list* dl_listing      (const char *path);
void      dl_diff         (dep_list **before, dep_list **after);


#define WATCH_USER       0
#define WATCH_DEPENDENCY 1

typedef struct watch {
    int type:1;               /* TODO: enum? */
    int is_directory:1;       /* a flag, a directory or not */

    uint32_t flags;           /* flags in the inotify format */
    char *filename;           /* file name of a watched file */
    int fd;                   /* file descriptor of a watched entry */

    union {
        dep_list *deps;       /* dependencies for an user-defined watch */
        struct watch *parent; /* parent watch for an automatic (dependency) watch */
    };
} watch;

typedef struct worker_sets {
    struct kevent *events;    /* kevent entries */
    struct watch *watches;    /* appropriate watches with additional info */
    size_t length;            /* size of active entries */
    size_t allocated;         /* size of allocated entries */
} worker_sets;


int watch_init_user       (watch *w, struct kevent *kv, const char *path, uint32_t flags, int index);
int watch_init_dependency (watch *w, struct kevent *kv, const char *path, uint32_t flags, int index);

void worker_sets_init   (worker_sets *ws, int fd);
int  worker_sets_extend (worker_sets *ws, int count);
void worker_sets_free   (worker_sets *ws);


#endif /* __WORKER_SETS_H__ */
