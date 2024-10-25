#include <common/rc.h>

inline void init_rc(volatile RefCount *rc)
{
    rc->count = 0;
}

inline void increment_rc(volatile RefCount *rc)
{
    __atomic_fetch_add(&rc->count, 1, __ATOMIC_ACQ_REL);
}

inline bool decrement_rc(volatile RefCount *rc)
{
    i64 r = __atomic_sub_fetch(&rc->count, 1, __ATOMIC_ACQ_REL);
    return r <= 0;
}
