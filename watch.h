#ifndef __WATCH_H__
#define __WATCH_H__

#include <sys/event.h> /* kevent */
#include <stdint.h>    /* uint32_t */
#include "dep-list.h"

#define WATCH_USER       0
#define WATCH_DEPENDENCY 1

typedef struct watch {
    int type;                 /* TODO: enum? */
    int is_directory;         /* a flag, a directory or not */

    uint32_t flags;           /* flags in the inotify format */
    char *filename;           /* file name of a watched file */
    int fd;                   /* file descriptor of a watched entry */

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
                uint32_t       flags,
                int            index);


#endif // __WATCH_H__
