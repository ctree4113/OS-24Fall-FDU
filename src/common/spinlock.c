#include <aarch64/intrinsic.h>
#include <common/spinlock.h>

void init_spinlock(SpinLock *lock) // 初始化自旋锁
{
    lock->locked = 0;
}

bool try_acquire_spinlock(SpinLock *lock) // 尝试获取自旋锁
{
    if (!lock->locked &&
        !__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        return true;
    } else {
        return false;
    }
}

void acquire_spinlock(SpinLock *lock) // 获取自旋锁
{
    while (!try_acquire_spinlock(lock))
        arch_yield();
}

void release_spinlock(SpinLock *lock) // 释放自旋锁
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
