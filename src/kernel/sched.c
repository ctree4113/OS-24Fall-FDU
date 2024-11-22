#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag; // 是否发生panic
extern Proc idle_proc[NCPU]; // 每个CPU的idle进程
extern void swtch(KernelContext** old_ctx, KernelContext* new_ctx); // 进程上下文切换

static SpinLock sched_lock; // 调度器锁
static Queue sched_queue; // 调度队列

static struct timer sched_timer[NCPU]; // CPU 定时器
static void time_sched() { // 调度定时器
    acquire_sched_lock();
    sched(RUNNABLE);
}

void init_sched() // 初始化调度器
{
    init_spinlock(&sched_lock); // 初始化调度锁
    queue_init(&sched_queue);   // 初始化调度队列

    for (int i = 0; i < NCPU; i++) { // 初始化调度定时器
        sched_timer[i].elapse = 20;
        sched_timer[i].handler = time_sched;
    }
}


void init_schinfo(struct schinfo* p) // 为进程初始化调度信息
{
    init_list_node(&p->sched_node);
}

Proc* thisproc() // 返回当前 CPU 上执行的进程
{
    return thiscpu->sched.proc;
}

void acquire_sched_lock()
{
    if (thisproc()->idle == false) acquire_spinlock(&thisproc()->lock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need ?

    release_spinlock(&sched_lock); // ？
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

bool _activate_proc(Proc *p, bool onalert)
{
    // TODO:(Lab5 new)
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEPING, do nothing if onalert or activate it if else, and return the corresponding value.
    acquire_spinlock(&p->lock);

    if (p->state == RUNNING || p->state == RUNNABLE || p->state == ZOMBIE) {
        release_spinlock(&p->lock);
        return false;
    }

    if (p->state == UNUSED || p->state == SLEEPING) {
        p->state = RUNNABLE;
        queue_push_lock(&sched_queue, &p->schinfo.sched_node);
        release_spinlock(&p->lock);
        return true;
    }

    if (p->state == DEEPSLEEPING) {
        if (onalert) { // onalert为true时忽略唤醒请求，适用于不可中断的信号量等待
            release_spinlock(&p->lock);
            return false;
        }

        p->state = RUNNABLE;
        queue_push_lock(&sched_queue, &p->schinfo.sched_node);
        release_spinlock(&p->lock);
        return true;
    }

    
}

static void update_this_state(enum procstate new_state) // 更新当前进程的状态为 new_state
{

    Proc* proc = thisproc();
    proc->state = new_state;
    if (new_state == SLEEPING || new_state == ZOMBIE) { // 如果进程状态更新为 SLEEPING 或 ZOMBIE, 则从调度队列中移除
        queue_detach_lock(&sched_queue, &proc->schinfo.sched_node);
    }
}

static Proc* pick_next() // 持锁从调度队列中挑选下一个执行的进程
{
    if (queue_empty(&sched_queue)) // 如果调度队列为空, 则返回 idle 进程
        return &idle_proc[cpuid()];

    queue_lock(&sched_queue);
    if (queue_empty(&sched_queue)) {
        queue_unlock(&sched_queue);
        return &idle_proc[cpuid()];
    }

    ListNode* node = queue_front(&sched_queue);
    ListNode* start = node;
    do {
        Proc* proc = container_of(node, Proc, schinfo.sched_node);
        ListNode* node_next = node->next;
        if (proc != thisproc() && proc->state == RUNNABLE && try_acquire_spinlock(&proc->lock)) {
            if (proc->state == RUNNABLE) { // 避免被其他CPU修改状态
                _queue_detach(&sched_queue, node);
                _queue_push(&sched_queue, node);
                queue_unlock(&sched_queue);
                return proc;
            }
        }
        if (node_next != queue_front(&sched_queue))
            node = node_next;
        else
            break;
    } while (node != start);

    queue_unlock(&sched_queue);

    return &idle_proc[cpuid()]; // 如果调度队列中没有可运行的进程, 则返回 idle 进程
}

// 将进程p更新为CPU选择的进程
// static void update_this_proc(Proc* p)
// {
//     auto c = thiscpu;
//     c->sched.proc = p;
// }

void sched(enum procstate new_state) // 持锁将当前处于 RUNNING 状态的进程更新为 new_state 状态
{
    // 获取当前执行的进程
    auto cpu = thiscpu;
    Proc* proc = thisproc();
    Proc* before, * next;

    if (proc->idle == false) { // 如果不是idle进程
        if (proc->killed && new_state != ZOMBIE) // 如果当前进程带有 killed 标记，且 new_state 不为 ZOMBIE，则直接返回
            return;

        ASSERT(proc->state == RUNNING); // 确保当前进程是 RUNNING 状态
        update_this_state(new_state);
        next = &idle_proc[cpuid()]; // 切换到idle进程
        cpu->sched.proc = next;
        cpu->sched.last_proc = proc; // 记录当前进程
        attach_pgdir(&next->pgdir);
        swtch(&proc->kcontext, next->kcontext);

        release_spinlock(&proc->lock);
    }
    else { // 如果是idle进程
        do {
            next = pick_next();
            if (next == proc) return; // 如果仍然是idle进程, 则退出
            ASSERT(next->state == RUNNABLE); // 确保下一个进程是 RUNNABLE 状态
            next->state = RUNNING;
            cpu->sched.proc = next;
            attach_pgdir(&next->pgdir);
            set_cpu_timer(&sched_timer[cpuid()]); // 启用调度定时器
            swtch(&proc->kcontext, next->kcontext);
            before = cpu->sched.last_proc; // 返回到idle进程

            release_spinlock(&before->lock);
        } while(1);
    }
}


u64 proc_entry(void (*entry)(u64), u64 arg) // 设置进程入口函数和参数
{
    release_spinlock(&thisproc()->lock);
    set_return_addr(entry);
    return arg;
}
