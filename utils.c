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
