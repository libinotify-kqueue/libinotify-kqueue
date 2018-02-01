/*******************************************************************************
  Copyright (c) 2014 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#include <sys/types.h>
#include <assert.h> /* assert */
#include <dirent.h> /* opendir */
#include <fcntl.h>  /* fcntl */
#include <unistd.h> /* close */

#include "compat.h"

DIR *
fdopendir (int fd)
{
    DIR *dir;
    char *dirpath = fd_getpath_cached (fd);
    if (dirpath == NULL) {
        return NULL;
    }

    dir = opendir (dirpath);

    /*
     * Dirty hack!!! Following code depends on libc private internals!!!
     * Historicaly directory file descriptor is first member of struct _dirdesc
     * (aka DIR) and named dd_fd. We can use this fact to overwrite newly
     * allocated file descriptor with user provided one. This allows fd get
     * closed on closedir() and do not be leaked than.
     */
    if (dir != NULL) {
        int oldfd = dirfd (dir);
        /* Ignore error as CLOEXEC is not strictly required by POSIX */
        fcntl (fd, F_SETFD, FD_CLOEXEC);
#ifdef DIR_HAVE_DD_FD
        dir->dd_fd = fd;
#else
        assert (oldfd == *(int *)dir);
        *(int *)dir = fd;
#endif
        close (oldfd);
    }

    return dir;
}
