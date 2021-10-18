/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>
  Copyright (c) 2014-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <sys/types.h>
#include <sys/uio.h>  /* iovec */

#include <dirent.h> /* DIR */
#include <errno.h>  /* errno */
#include <stdio.h>  /* fprintf */
#include <string.h> /* strerror */
#include <pthread.h>

#include "config.h"

extern const struct timespec *zero_tsp;

/**
 * Print an error message, if allowed.
 *
 * Print a file name, line number and errno-based error description as well.
 * The library should be built with --enable-perrors configure option.
 *
 * @param[in] msg A message format to print followed by a set of parameters to
 *                include in the message, according to the format string.
 **/
#ifdef ENABLE_PERRORS
extern pthread_mutex_t perror_msg_mutex;
char *perror_msg_printf (const char *fmt, ...);
#define perror_msg(msg)                                                 \
do {                                                                    \
    int saved_errno_ = errno;                                           \
    char *buf_;                                                         \
    pthread_mutex_lock (&perror_msg_mutex);                             \
    buf_ = perror_msg_printf msg;                                       \
    fprintf (stderr, "%s.%d: %s: %d (%s)\n", __FILE__, __LINE__, buf_,  \
             saved_errno_, strerror (saved_errno_));                    \
    pthread_mutex_unlock (&perror_msg_mutex);                           \
    errno = saved_errno_;                                               \
} while (0)
#else
#define perror_msg(msg)
#endif

struct inotify_event* create_inotify_event (int         wd,
                                            uint32_t    mask,
                                            uint32_t    cookie,
                                            const char *name,
                                            size_t     *event_len);

ssize_t sendv (int fd, struct iovec iov[], int iovcnt, int flags);

int is_opened (int fd);
int is_deleted (int fd);
int set_cloexec_flag (int fd, int value);
int set_nonblock_flag (int fd, int value);
int set_sndbuf_size (int fd, int len);
int dup_cloexec (int oldd);
DIR *fdreopendir (int oldd);

#endif /* __UTILS_H__ */
