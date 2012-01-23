#include <assert.h>
#include <string.h> /* memset */

#include "barriers.h"


static void
ik_barrier_impl_init (ik_barrier_impl *impl, int count)
{
    assert (impl != NULL);

    memset (impl, 0, sizeof (ik_barrier_impl));
    impl->count = count;

    pthread_mutex_init (&impl->mtx, NULL);
    pthread_cond_init  (&impl->cnd, NULL);
}


static void
ik_barrier_impl_wait (ik_barrier_impl *impl)
{
    assert (impl != NULL);

    if (impl->passed == 0) {
        pthread_mutex_lock (&impl->mtx);

        if (impl->sleeping == impl->count - 1) {
            impl->passed = 1;
            pthread_mutex_unlock (&impl->mtx);
            pthread_cond_broadcast (&impl->cnd);
        } else {
            ++impl->sleeping;

            while (impl->passed == 0) {
                pthread_cond_wait (&impl->cnd, &impl->mtx);
            }

            --impl->sleeping;
            pthread_mutex_unlock (&impl->mtx);
        }
    }

    while (impl->sleeping != 0);
}


static void
ik_barrier_impl_destroy (ik_barrier_impl *impl)
{
    assert (impl != NULL);

    pthread_cond_destroy  (&impl->cnd);
    pthread_mutex_destroy (&impl->mtx);

    impl->count = 0;
    impl->sleeping = 0;
    impl->passed = 0;
}


void
ik_barrier_init (ik_barrier *b, int n)
{
    assert (b != NULL);
#ifndef WITHOUT_BARRIERS
    pthread_barrier_init (&b->impl, NULL, n);
#else
    ik_barrier_impl_init (&b->impl, n);
#endif
}


void
ik_barrier_wait (ik_barrier *b)
{
    assert (b != NULL);;
#ifndef WITHOUT_BARRIERS
    pthread_barrier_wait (&b->impl);
#else
    ik_barrier_impl_wait (&b->impl);
#endif
}


void
ik_barrier_destroy (ik_barrier *b)
{
    assert (b != NULL);;
#ifndef WITHOUT_BARRIERS
    pthread_barrier_destroy (&b->impl);
#else
    ik_barrier_impl_destroy (&b->impl);
#endif
}
