/*******************************************************************************
  Copyright (c) 2015-2018 Vladimir Kondratyev <vladimir@kondratyev.su>
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
#include <assert.h>
#include <pthread.h>

#include "config.h"

extern pthread_mutex_t ik_atomic_mutex;

#define	_Atomic(T)		T volatile
typedef	_Atomic(unsigned int)		atomic_uint;
#define ATOMIC_VAR_INIT(value)          (value)
#define	atomic_init(object, value)	(*(object) = (value))
#define atomic_load(object) \
    atomic_fetch_add_impl((object), 0, sizeof (*(object)))
#define atomic_fetch_add(object, operand) \
    atomic_fetch_add_impl((object), (operand), sizeof (*(object)))
#define atomic_fetch_sub(object, operand) \
    atomic_fetch_add_impl((object), -(operand), sizeof (*(object)))

static inline uint64_t
atomic_fetch_add_impl (volatile void *object, uint64_t operand, int bytes)
{
    uint64_t ret = 0;
    pthread_mutex_lock (&ik_atomic_mutex);
    switch (bytes) {
    case 8:
        ret = *((uint64_t *)object);
        *((uint64_t *)object) += operand;
        break;
    case 4:
        ret = *((uint32_t *)object);
        *((uint32_t *)object) += operand;
        break;
    case 2:
        ret = *((uint16_t *)object);
        *((uint16_t *)object) += operand;
        break;
    case 1:
        ret = *((uint8_t *)object);
        *((uint8_t *)object) += operand;
        break;
    default:
        /* Not implemented */
        assert (0);
    }
    pthread_mutex_unlock (&ik_atomic_mutex);
    return ret;
}
