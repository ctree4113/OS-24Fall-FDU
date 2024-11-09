#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag;
extern SpinLock proc_lock; // 进程树锁

static SpinLock sched_lock; // 调度器锁
static Queue sched_queue; // 调度队列

static struct timer sched_timer[NCPU]; // CPU 定时器
static void time_sched(struct timer* t) { sched(RUNNABLE); }

extern void swtch(KernelContext** old_ctx, KernelContext* new_ctx);

void init_sched() // 初始化调度器
{
    init_spinlock(&sched_lock); // 初始化调度锁
    queue_init(&sched_queue);   // 初始化调度队列

    for (int i = 0; i < NCPU; i++) { // 初始化调度定时器
        sched_timer[i].elapse = 20;
        sched_timer[i].handler = time_sched;
    }
}


void init_schinfo(struct schinfo* p)
{
    return;
}

Proc* thisproc() // 返回当前 CPU 上执行的进程
{
    return thiscpu->sched.proc;
}

void acquire_sched_lock()
{
    acquire_spinlock(&sched_lock);
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
}

static void update_this_state(enum procstate new_state) // 更新当前进程的状态为 new_state
{

    Proc* proc = thisproc();
    proc->state = new_state;
    if (new_state == SLEEPING || new_state == ZOMBIE) { // 如果进程状态更新为 SLEEPING 或 ZOMBIE, 则从调度队列中移除
        queue_detach_lock(&sched_queue, &proc->schinfo.sched_node);
    }
}

static Proc* pick_next() // 从调度队列中挑选下一个执行的进程
{
    if (queue_empty(&sched_queue)) // 如果调度队列为空, 则返回 idle 进程
        return thiscpu->sched.idle_proc;

    queue_lock(&sched_queue);

    ListNode* node = queue_front(&sched_queue);
    ListNode* start = node;
    do {
        Proc* proc = container_of(node, Proc, schinfo.sched_node);
        if (proc->state == RUNNABLE) {
            _queue_detach(&sched_queue, node);
            _queue_push(&sched_queue, node);
            queue_unlock(&sched_queue);
            return proc;
        }
        node = node->next;
    } while (node != start);

    queue_unlock(&sched_queue);

    return thiscpu->sched.idle_proc; // 如果调度队列中没有可运行的进程, 则返回 idle 进程
}

// 将进程p更新为CPU选择的进程
static void update_this_proc(Proc* p)
{
    auto c = thiscpu;
    c->sched.proc = p;
}

void sched(enum procstate new_state) // 将当前处于 RUNNING 状态的进程更新为 new_state 状态
{
    acquire_spinlock(&proc_lock);

    Proc* this_proc = thisproc();
    if (this_proc->killed && new_state != ZOMBIE) { // 如果当前进程带有 killed 标记，且 new_state 不为 ZOMBIE，则直接返回
        release_spinlock(&proc_lock);
        return;
    }

    ASSERT(this_proc->state == RUNNING);
    update_this_state(new_state);
    Proc* next_proc = pick_next();
    update_this_proc(next_proc);
    ASSERT(next_proc->state == RUNNABLE);
    next_proc->state = RUNNING;

    release_spinlock(&proc_lock);

    set_cpu_timer(&sched_timer[cpuid()]); // 设置调度定时器

    if (next_proc != this_proc) {
        attach_pgdir(&next_proc->pgdir);
        swtch(&this_proc->kcontext, next_proc->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg) // 设置进程入口函数和参数
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
