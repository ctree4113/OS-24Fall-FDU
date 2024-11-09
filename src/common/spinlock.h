#pragma once

#include <common/defines.h>
#include <aarch64/intrinsic.h>

typedef struct { // 自旋锁
    volatile bool locked;
} SpinLock;

void init_spinlock(SpinLock*);                     // 初始化自旋锁
WARN_RESULT bool try_acquire_spinlock(SpinLock*);  // 尝试获取自旋锁
void acquire_spinlock(SpinLock*);                  // 获取自旋锁
void release_spinlock(SpinLock*);                  // 释放自旋锁
