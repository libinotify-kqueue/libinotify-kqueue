#ifndef __WATCH_H__
#define __WATCH_H__

#include <sys/event.h> /* kevent */
#include <stdint.h>    /* uint32_t */
#include <dirent.h>    /* ino_t */

// TODO: Worker is a very bad dependency here
#include "dep-list.h"

#define WATCH_USER       0
#define WATCH_DEPENDENCY 1


typedef struct watch {
    int type;                 /* TODO: enum? */
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

// TODO: enum for watch type
int watch_init (watch         *w,
                int            watch_type,
                struct kevent *kv,
                const char    *path,
                const char    *entry_name,
                uint32_t       flags,
                int            index);

void watch_free (watch *w);


#endif // __WATCH_H__
