#ifndef __RT_RETAINABLE_H__
#define __RT_RETAINABLE_H__

#include "rtAtomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtRetainable
{
  atomic_int refCount;
} rtRetainable;

#define rtRetainable_retain(X) if (X) rtRetainable_retainInternal((rtRetainable *)(X))
#define rtRetainable_release(X, D) if (X) rtRetainable_releaseInternal((rtRetainable *)(X), D)

void rtRetainable_retainInternal(rtRetainable* r);
void rtRetainable_releaseInternal(rtRetainable* r, void (*Destructor)(rtRetainable*));

#ifdef __cplusplus
}
#endif

#endif
