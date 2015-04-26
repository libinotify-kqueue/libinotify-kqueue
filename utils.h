/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

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

#include <sys/stat.h> /* S_ISDIR */
#include <sys/uio.h>  /* iovec */

#include <stdint.h> /* uint32_t */
#include <pthread.h>

struct inotify_event* create_inotify_event (int         wd,
                                            uint32_t    mask,
                                            uint32_t    cookie,
                                            const char *name,
                                            size_t     *event_len);

ssize_t safe_read   (int fd, void *data, size_t size);
ssize_t safe_write  (int fd, const void *data, size_t size);
ssize_t safe_writev (int fd, const struct iovec iov[], int iovcnt);

int is_opened (int fd);
int is_deleted (int fd);
int set_cloexec_flag (int fd, int value);

void perror_msg (const char *msg, ...);

#endif /* __UTILS_H__ */
