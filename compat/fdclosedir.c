/*******************************************************************************
  Copyright (c) 2018 Vladimir Kondratyev <vladimir@kondratyev.su>
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
#include <dirent.h> /* closedir */
#include <unistd.h> /* dup */

#include "config.h"

int
fdclosedir (DIR *dir)
{
    /*
     * Dirty hack!!! Following code depends on libc private internals!!!
     * Historicaly directory file descriptor is first member of struct _dirdesc
     * (aka DIR) and named dd_fd. We can use this fact to overwrite newly
     * allocated file descriptor with user provided one. This allows us to
     * provide closedir() with faked dirfd and avoid closing of real one.
     */
    int fd = dirfd (dir);
#ifdef DIR_HAVE_DD_FD
    /* Don't bother about error */
    dir->dd_fd = dup (fd);
#else
    assert (fd == *(int *)dir);
    *(int *)dir = dup (fd);
#endif
    closedir (dir);

    return fd;
}
