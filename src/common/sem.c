#include <common/sem.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/list.h>

void init_sem(Semaphore* sem, int val) // 初始化信号量
{
    sem->val = val;
    init_spinlock(&sem->lock);
    init_list_node(&sem->sleeplist);
}

void _lock_sem(Semaphore *sem) // 为信号量sem获取锁
{
    acquire_spinlock(&sem->lock);
}

void _unlock_sem(Semaphore *sem) // 释放信号量sem的锁
{
    release_spinlock(&sem->lock);
}

bool _get_sem(Semaphore *sem) // 持锁尝试获取信号量sem
{
    bool ret = false;
    if (sem->val > 0) {
        sem->val--;
        ret = true;
    }
    return ret;
}

int _query_sem(Semaphore *sem) // 查询信号量sem的值
{
    return sem->val;
}

int get_all_sem(Semaphore *sem) // 获取sem的所有信号量值
{
    int ret = 0;
    _lock_sem(sem);
    if (sem->val > 0) {
        ret = sem->val;
        sem->val = 0;
    }
    _unlock_sem(sem);
    return ret;
}

int post_all_sem(Semaphore *sem) // 释放sem的所有信号量
{
    int ret = -1;
    _lock_sem(sem);
    do
        _post_sem(sem), ret++;
    while (!_get_sem(sem));
    _unlock_sem(sem);
    return ret;
}

bool _wait_sem(Semaphore *sem, bool alertable) // 持锁等待信号量sem
{
    if (--sem->val >= 0) { // 信号量值减1, 如果成功获取信号量则直接返回true
        release_spinlock(&sem->lock);
        return true;
    }

    // 否则，初始化一个等待数据
    WaitData *wait = kalloc(sizeof(WaitData));
    wait->proc = thisproc();
    wait->up = false;

    _insert_into_list(&sem->sleeplist, &wait->slnode); // 将等待数据插入到信号量的休眠链表
    
    acquire_sched_lock();
    release_spinlock(&sem->lock);
    sched(alertable ? SLEEPING : DEEPSLEEPING);  // 设置当前进程休眠并启用调度
    acquire_spinlock(&sem->lock);
    if (!wait->up) // 如果进程不是正常被信号量唤醒
    {
        ASSERT(++sem->val <= 0); // 信号量值加1（恢复信号量的值）
        _detach_from_list(&wait->slnode); // 从休眠链表中移除等待数据
    }
    release_spinlock(&sem->lock);
    bool ret = wait->up; // 返回唤醒状态
    kfree(wait);
    return ret;
}

void _post_sem(Semaphore *sem) // 释放信号量sem
{
    if (++sem->val <= 0) { // 信号量值加1，如果有进程在等待
        ASSERT(!_empty_list(&sem->sleeplist)); //确保休眠链表非空
        auto wait = container_of(sem->sleeplist.prev, WaitData, slnode); // 获取休眠链表上的第一个等待数据
        wait->up = true; // 将该等待数据标记为已唤醒
        _detach_from_list(&wait->slnode); // 从休眠链表中移除该等待数据
        activate_proc(wait->proc); // 唤醒该进程
    }
}