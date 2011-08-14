#include <pthread.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void acquire_log_lock ()
{
    pthread_mutex_lock (&log_mutex);
}

void release_log_lock ()
{
    pthread_mutex_unlock (&log_mutex);
}

unsigned int current_thread ()
{
#ifdef __linux__
    return static_cast<unsigned int>(pthread_self ());
#elif defined (__NetBSD__)
    return reinterpret_cast<unsigned int>(pthread_self ());
#else
    error Currently unsupported
#endif
}
