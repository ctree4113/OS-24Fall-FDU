#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>

Proc root_proc;
void kernel_entry();

SpinLock proc_lock;

#define MAX_PID 32768 // PID 最大值
#define PID_BITMAP_SIZE (MAX_PID / 64) // PID 位图大小

static u64 pid_bitmap[PID_BITMAP_SIZE];
static SpinLock pid_lock;

void init_pid_allocator()
{
    init_spinlock(&pid_lock);
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    // 将 PID 0 标记为已使用（通常保留给内核）
    pid_bitmap[0] |= 1ULL;
}

int alloc_pid() // PID 分配器
{
    acquire_spinlock(&pid_lock);
    for (int i = 0; i < PID_BITMAP_SIZE; i++) {
        if (pid_bitmap[i] != ~0ULL) {
            for (int j = 0; j < 64; j++) {
                if ((pid_bitmap[i] & (1ULL << j)) == 0) {
                    pid_bitmap[i] |= (1ULL << j);
                    int pid = i * 64 + j + 1;
                    release_spinlock(&pid_lock);
                    return pid;
                }
            }
        }
    }
    release_spinlock(&pid_lock);
    return -1;  // 没有可用的 PID
}

void free_pid(int pid) // PID 回收
{
    if (pid <= 0 || pid >= MAX_PID) return;
    acquire_spinlock(&pid_lock);
    pid--;
    pid_bitmap[pid / 64] &= ~(1ULL << (pid % 64));
    release_spinlock(&pid_lock);
}

void init_kproc() // 初始化内核进程
{
    init_spinlock(&proc_lock);

    init_pid_allocator();

    for (int i = 0; i < NCPU; i++) { // 初始化每个 CPU 的 idle 进程
        auto p = create_proc();
        p->idle = true;
        p->state = RUNNING;
        cpus[i].sched.proc = p;
        cpus[i].sched.idle_proc = p;
    }

    // 初始化 root_proc
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc* p) // 初始化进程
{
    acquire_spinlock(&proc_lock);

    p->pid = alloc_pid();
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;
    p->state = UNUSED;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);
    p->kcontext = kalloc_page() + PAGE_SIZE - sizeof(KernelContext); // 分配内核进程上下文
    
    release_spinlock(&proc_lock);
}

Proc* create_proc() // 创建进程
{
    Proc* proc = kalloc(sizeof(Proc));
    init_proc(proc);

    return proc;
}

void set_parent_to_this(Proc* proc) // 设置父进程为当前进程
{
    acquire_spinlock(&proc_lock);

    Proc* p = thisproc();
    proc->parent = p;
    _insert_into_list(&p->children, &proc->ptnode);

    release_spinlock(&proc_lock);
}

int start_proc(Proc* p, void (*entry)(u64), u64 arg) // 配置进程以启动
{
    acquire_spinlock(&proc_lock);

    if (p->parent == NULL) {
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
    }

    p->kcontext->x30 = (u64)proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;

    release_spinlock(&proc_lock);

    activate_proc(p);

    return p->pid;
}

int wait(int* exitcode) // 等待子进程退出
{
    acquire_spinlock(&proc_lock);

    Proc* p = thisproc();
    if (_empty_list(&p->children)) { // 如果没有子进程, 则返回 -1
        release_spinlock(&proc_lock);
        return -1;
    }

    while (1) { // 如果存在子进程, 则遍历所有子进程
        Proc* p = thisproc();
        ListNode* next_node = p->children.next;
        ListNode* end_node = &p->children;

        do {
            Proc* proc = container_of(next_node, Proc, ptnode);
            if (proc->state == ZOMBIE) { // 如果子进程处于 ZOMBIE 状态, 则回收子进程
                int pid = proc->pid;
                if (exitcode != 0)
                    *exitcode = proc->exitcode;
                _detach_from_list(&proc->ptnode); // 从父进程的孩子链表中移除
                kfree_page((void*)round_up((u64)proc->kcontext - PAGE_SIZE, PAGE_SIZE)); // 释放进程的内存
               kfree(proc); // 释放进程结构体
                release_spinlock(&proc_lock);
                return pid;
            }
            next_node = next_node->next;
        } while (next_node != end_node);

        release_spinlock(&proc_lock);
        wait_sem(&p->childexit); // 等待子进程退出
        acquire_spinlock(&proc_lock);
    }
}

NO_RETURN void exit(int code) // 退出当前进程
{
    Proc* p = thisproc();
    if (p == &root_proc) PANIC(); // 不能退出 root_proc

    if (_empty_list(&p->children) == false) { // 如果有子进程, 则将这些子进程的父进程设置为 root_proc
        ListNode* next_node = p->children.next;
        while (1) {
            ListNode* nn_node = next_node->next;
            Proc* proc = container_of(next_node, Proc, ptnode);
            proc->parent = &root_proc;
            _insert_into_list(&root_proc.children, &proc->ptnode);
            activate_proc(&root_proc);
            if (nn_node == &p->children) break;
            else next_node = nn_node;
        }
    }

    post_sem(&p->parent->childexit); // 释放父进程信号量
    p->exitcode = code;

    acquire_sched_lock();

    sched(ZOMBIE);
    free_pid(p->pid); // 释放 PID

    PANIC();
}