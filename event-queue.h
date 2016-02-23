/*******************************************************************************
  Copyright (c) 2016 Vladimir Kondratiev <wulf@cicgroup.ru>

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

#ifndef __EVENT_QUEUE_H__
#define __EVENT_QUEUE_H__

#include <sys/types.h> /* uint32_t */
#include <sys/uio.h>   /* iovec */

typedef struct event_queue {
    struct iovec *iov; /* inotify events to send */
    int count;         /* number of events enqueued */
    int allocated;     /* number of iovs allocated */
} event_queue;

void event_queue_init (event_queue *eq);
void event_queue_free (event_queue *eq);

int  event_queue_enqueue (event_queue *eq,
                          int          wd,
                          uint32_t     mask,
                          uint32_t     cookie,
                          const char  *name);
void event_queue_flush   (event_queue *eq, int fd[2], size_t sbspace);

#endif /* __EVENT_QUEUE_H__ */
