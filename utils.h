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

#include <stdint.h> /* uint32_t */
#include <pthread.h>


/* struct kevent is declared slightly differently on the different BSDs.
 * This macros will help to avoid cast warnings on the supported platforms.
 */
#if defined(__NetBSD__)
#  define INDEX_TO_UDATA(X) (X)
#  define UDATA_TO_INDEX(X) (X)
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
#  define INDEX_TO_UDATA(X) ((void *)(uintptr_t)X)
#  define UDATA_TO_INDEX(X) ((uintptr_t)X)
#else
#  error Currently unsupported
#endif


char* path_concat (const char *dir, const char *file);

struct inotify_event* create_inotify_event (int         wd,
                                            uint32_t    mask,
                                            uint32_t    cookie,
                                            const char *name,
                                            int        *event_len);

int safe_read  (int fd, void *data, size_t size);
int safe_write (int fd, const void *data, size_t size);

int is_opened (int fd);

void perror_msg (const char *msg, ...);

#endif /* __UTILS_H__ */
