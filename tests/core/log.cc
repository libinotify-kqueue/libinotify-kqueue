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

#include <pthread.h>
#if defined(__FreeBSD__)
#include <sys/types.h>
#endif

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void acquire_log_lock ()
{
    pthread_mutex_lock (&log_mutex);
}

void release_log_lock ()
{
    pthread_mutex_unlock (&log_mutex);
}

unsigned long current_thread ()
{
#ifdef __linux__
    return static_cast<uintptr_t>(pthread_self ());
#elif defined (__NetBSD__) || defined (__OpenBSD__) || defined(__APPLE__)
    return reinterpret_cast<unsigned long>(pthread_self ());
#elif defined (__FreeBSD__)
    return reinterpret_cast<uintptr_t>(pthread_self ());
#else
#   error Currently unsupported
#endif
}
