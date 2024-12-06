#pragma once

#include <common/list.h>

struct Proc;

typedef struct {
    bool up;           // 是否唤醒
    struct Proc *proc; // 需要休眠的进程
    ListNode slnode;   // 休眠链表节点
} WaitData; // 等待数据

typedef struct {
    SpinLock lock;      // 信号量锁
    int val;            // 信号量值，信号量为0时，表示没有进程可以访问资源
    ListNode sleeplist; // 休眠链表
} Semaphore; // 信号量

void init_sem(Semaphore *, int val);                     // 初始化信号量
void _post_sem(Semaphore *);                             // 唤醒信号量
WARN_RESULT bool _wait_sem(Semaphore *, bool alertable); // 等待信号量
bool _get_sem(Semaphore *);                              // 获取信号量
WARN_RESULT int _query_sem(Semaphore *);                 // 查询信号量
void _lock_sem(Semaphore *);                             // 锁定信号量
void _unlock_sem(Semaphore *);                           // 解锁信号量
int get_all_sem(Semaphore *);                            // 获取所有信号量
int post_all_sem(Semaphore *);                           // 释放所有信号量

#define wait_sem(sem)                      \
    ({                                     \
        _lock_sem(sem);                    \
        bool __ret = _wait_sem(sem, true); \
        __ret;                             \
    }) // 可中断的信号量等待
#define unalertable_wait_sem(sem)           \
    ASSERT(({                               \
        _lock_sem(sem);                     \
        bool __ret = _wait_sem(sem, false); \
        __ret;                              \
    })) // 不可中断的信号量等待
#define post_sem(sem)     \
    ({                    \
        _lock_sem(sem);   \
        _post_sem(sem);   \
        _unlock_sem(sem); \
    }) // 释放信号量
#define get_sem(sem)                \
    ({                              \
        _lock_sem(sem);             \
        bool __ret = _get_sem(sem); \
        _unlock_sem(sem);           \
        __ret;                      \
    }) // 获取信号量
#define sleep_sem(sem, lock)    \
    ({                         \
        release_spinlock(lock); \
        wait_sem(sem);        \
        acquire_spinlock(lock); \
    }) // 释放锁并在sem上休眠, 醒来时重新获取锁
#define wakeup_sem(sem) post_sem(sem) // 唤醒所有在sem上休眠的进程

#define SleepLock Semaphore                                            // 休眠锁
#define init_sleeplock(lock) init_sem(lock, 1)                         // 初始化休眠锁
#define acquire_sleeplock(lock) wait_sem(lock)                         // 获取休眠锁（可中断）
#define unalertable_acquire_sleeplock(lock) unalertable_wait_sem(lock) // 获取休眠锁（不可中断）
#define release_sleeplock(lock) post_sem(lock)                         // 释放休眠锁
