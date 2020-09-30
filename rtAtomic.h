#ifndef __RT_ATOMIC_H__
#define __RT_ATOMIC_H__

#if defined(__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 8))) && !defined(NO_ATOMICS)
  #define RT_ATOMIC_HAS_ATOMIC_FETCH
#elif defined(__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 0))) && !defined(NO_ATOMICS)
  #define RT_ATOMIC_HAS_SYNC_FETCH
#endif

#if defined(RT_ATOMIC_HAS_ATOMIC_FETCH)
  #include <stdatomic.h>
#elif defined(RT_ATOMIC_HAS_SYNC_FETCH)
  #define atomic_int volatile int
#else
  #include <pthread.h>
  #define atomic_int volatile int
  static pthread_mutex_t g_atomic_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#endif

static inline void rt_atomic_fetch_add(atomic_int* var, int value)
{
#if defined(RT_ATOMIC_HAS_ATOMIC_FETCH)
    __atomic_fetch_add(var, value, __ATOMIC_SEQ_CST);
#elif defined(RT_ATOMIC_HAS_SYNC_FETCH)
    __sync_fetch_and_add(var, value);
#else
    pthread_mutex_lock(&g_atomic_mutex);
    if(NULL != var)
        *(var) = *(var) + value;
    pthread_mutex_unlock(&g_atomic_mutex);
#endif
}

static inline void rt_atomic_fetch_sub(atomic_int* var, int value)
{
#if defined(RT_ATOMIC_HAS_ATOMIC_FETCH)
    __atomic_fetch_sub(var, value, __ATOMIC_SEQ_CST);
#elif defined(RT_ATOMIC_HAS_SYNC_FETCH)
    __sync_fetch_and_sub(var, value);
#else
    pthread_mutex_lock(&g_atomic_mutex);
    if(NULL != var)
        *(var) = *(var) - value;
    pthread_mutex_unlock(&g_atomic_mutex);
#endif
}

#endif
