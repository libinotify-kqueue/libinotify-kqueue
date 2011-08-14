#include <unistd.h> /* read, write */
#include <errno.h>  /* EINTR */
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen */

#include "utils.h"

char*
path_concat (const char *dir, const char *file)
{
    int dir_len = strlen (dir);
    int file_len = strlen (file);
    // TODO: check allocation
    char *path = malloc (dir_len + file_len + 2);

    strcpy (path, dir);

    if (dir[dir_len - 1] != '/') {
        ++dir_len;
        path[dir_len - 1] = '/';
    }

    strcpy (path + dir_len, file);

    return path;
}


#define SAFE_GENERIC_OP(fcn, fd, data, size)    \
    while (size > 0) {                          \
        ssize_t retval = fcn (fd, data, size);  \
        if (retval == -1) {                     \
            if (errno == EINTR) {               \
                continue;                       \
            } else {                            \
                return -1;                      \
            }                                   \
        }                                       \
        size -= retval;                         \
        data += retval;                         \
    }                                           \
    return 0;


int
safe_read  (int fd, void *data, size_t size)
{
    SAFE_GENERIC_OP (read, fd, data, size);
}

int
safe_write (int fd, const void *data, size_t size)
{
    SAFE_GENERIC_OP (write, fd, data, size);
}
