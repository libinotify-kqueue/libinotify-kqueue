/*******************************************************************************
  Copyright (c) 2011-2014 Dmitry Matveev <me@dmitrymatveev.co.uk>
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

#include <assert.h>
#include <pthread.h>
#include <string.h> /* memset */

#include "compat.h"

/**
 * Initialize a barrier
 *
 * @param[in] impl   A pointer to barrier
 * @param[in] attr   A barrier attributes (not implemented)
 * @param[in] count  The number of threads to wait on the barrier
 **/
void
pthread_barrier_init (pthread_barrier_t *impl,
                      const pthread_barrierattr_t *attr,
                      unsigned count)
{
    assert (impl != NULL);

    memset (impl, 0, sizeof (pthread_barrier_t));
    impl->count = count;

    pthread_mutex_init (&impl->mtx, NULL);
    pthread_cond_init  (&impl->cnd, NULL);
}


/**
 * Wait on a barrier.
 *
 * If this thread is not the last expected one, it will be blocked
 * until all the expected threads will check in on the barrier.
 * Otherwise the barrier will be marked as passed and all blocked
 * threads will be unlocked.
 *
 * This barrier implementation is based on:
 *   http://siber.cankaya.edu.tr/ozdogan/GraduateParallelComputing.old/ceng505/node94.html
 *
 * @param[in] impl  A pointer to barrier
 **/
void
pthread_barrier_wait (pthread_barrier_t *impl)
{
    while (impl->entered == 0 && impl->sleeping != 0);

    pthread_mutex_lock (&impl->mtx);
    impl->entered++;
    if (impl->entered == impl->count) {
        impl->entered = 0;
        pthread_cond_broadcast (&impl->cnd);
    } else {
        ++impl->sleeping;
        while (pthread_cond_wait (&impl->cnd, &impl->mtx) != 0
               && impl->entered != 0);
        --impl->sleeping;
    }
    pthread_mutex_unlock (&impl->mtx);
}

/**
 * Destroy the barrier and all associated resources.
 *
 * @param[in] impl  A pointer to barrier
 **/
void
pthread_barrier_destroy (pthread_barrier_t *impl)
{
    assert (impl != NULL);

    pthread_cond_destroy  (&impl->cnd);
    pthread_mutex_destroy (&impl->mtx);

    impl->count = 0;
    impl->entered = 0;
    impl->sleeping = 0;
}
