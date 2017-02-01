/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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

#ifndef __UTILS_H__
#define __UTILS_H__

#include "config.h"
#include "compat.h"

#include <sys/stat.h> /* S_ISDIR */
#include <sys/uio.h>  /* iovec */

#include <errno.h>  /* errno */
#include <stdio.h>  /* fprintf */
#include <string.h> /* strerror */
#include <pthread.h>

/**
 * Print an error message, if allowed.
 *
 * Print a file name, line number and errno-based error description as well.
 * The library should be built with --enable-perrors configure option.
 *
 * @param[in] msg A message format to print.
 * @param[in] ... A set of parameters to include in the message, according
 *      to the format string.
 **/
#ifdef ENABLE_PERRORS
#define perror_msg(msg, ...)                                            \
do {                                                                    \
    int saved_errno_ = errno;                                           \
    fprintf (stderr, "%s.%d: " msg ": %d (%s)\n", __FILE__, __LINE__,   \
             ##__VA_ARGS__, errno, strerror (errno));                   \
    errno = saved_errno_;                                               \
} while (0)
#else
#define perror_msg(msg, ...)
#endif

struct inotify_event* create_inotify_event (int         wd,
                                            uint32_t    mask,
                                            uint32_t    cookie,
                                            const char *name,
                                            size_t     *event_len);

ssize_t safe_read   (int fd, void *data, size_t size);
ssize_t safe_write  (int fd, const void *data, size_t size);
ssize_t safe_send   (int fd, const void *data, size_t size, int flags);
ssize_t safe_writev (int fd, const struct iovec iov[], int iovcnt);
ssize_t safe_sendv (int fd, struct iovec iov[], int iovcnt, int flags);
ssize_t sendv (int fd, struct iovec iov[], int iovcnt, int flags);

int is_opened (int fd);
int is_deleted (int fd);
int set_cloexec_flag (int fd, int value);
int set_nonblock_flag (int fd, int value);
int dup_cloexec (int oldd);

#endif /* __UTILS_H__ */
