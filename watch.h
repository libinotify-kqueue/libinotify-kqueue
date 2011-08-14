#ifndef __WATCH_H__
#define __WATCH_H__

#include <sys/event.h> /* kevent */
#include <stdint.h>    /* uint32_t */
#include <dirent.h>    /* ino_t */

#include "dep-list.h"

typedef enum watch_type {
    WATCH_USER,
    WATCH_DEPENDENCY,
} watch_type_t;


typedef struct watch {
    watch_type_t type;        /* a type of a watch */
    int is_directory;         /* a flag, a directory or not */

    uint32_t flags;           /* flags in the inotify format */
    char *filename;           /* file name of a watched file
                               * NB: an entry file name for dependencies! */
    int fd;                   /* file descriptor of a watched entry */
    ino_t inode;              /* inode number for the watched entry */

    struct kevent *event;     /* a pointer to the associated kevent */

    union {
        dep_list *deps;       /* dependencies for an user-defined watch */
        struct watch *parent; /* parent watch for an automatic (dependency) watch */
    };
} watch;

int watch_init (watch         *w,
                watch_type_t   watch_type,
                struct kevent *kv,
                const char    *path,
                const char    *entry_name,
                uint32_t       flags,
                int            index);

void watch_free (watch *w);


#endif /* __WATCH_H__ */
