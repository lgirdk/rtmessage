#include <rtRetainable.h>

void rtRetainable_retainInternal(rtRetainable* r)
{
    rt_atomic_fetch_add(&r->refCount, 1);
}

void rtRetainable_releaseInternal(rtRetainable* r, void (*destructor)(rtRetainable*))
{
    rt_atomic_fetch_sub(&r->refCount, 1);

    if(r->refCount == 0)
    {
        destructor(r);
    }
}
