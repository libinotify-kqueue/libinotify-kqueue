#ifndef __BARRIERS_H__
#define __BARRIERS_H__

#include <pthread.h>

typedef struct {
    int count;
    volatile int sleeping;
    volatile int passed;

    pthread_mutex_t mtx;
    pthread_cond_t  cnd;
} ik_barrier_impl;

#define WITHOUT_BARRIERS

typedef struct {
#ifndef WITHOUT_BARRIERS    
    pthread_barrier_t impl;
#else
    ik_barrier_impl impl;
#endif    
} ik_barrier;

void ik_barrier_init    (ik_barrier *b, int n);
void ik_barrier_wait    (ik_barrier *b);
void ik_barrier_destroy (ik_barrier *b);

#endif /* __BARRIERS_H__ */
