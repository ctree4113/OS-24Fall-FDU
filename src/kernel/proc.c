#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>

Proc root_proc;       // 根进程
Proc idle_proc[NCPU]; // 每个CPU的idle进程
void kernel_entry();  // 内核入口

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
    init_pid_allocator();

    for (int i = 0; i < NCPU; i++) { // 初始化每个 CPU 的 idle 进程
        Proc* p = &idle_proc[i];
        init_proc(p);
        p->idle = true;
        p->state = RUNNING;
        cpus[i].sched.proc = p;
        kfree_page(p->trapcontext); // 释放中断上下文
        p->trapcontext = NULL;
        p->kcontext = NULL;
    }

    // 初始化 root_proc
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 0);
}

void init_proc(Proc* p) // 初始化进程
{
    init_spinlock(&p->lock);

    p->pid = alloc_pid();
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;
    p->state = UNUSED;
    init_sem(&p->childexit, 0);    // 初始化子进程退出信号量
    init_list_node(&p->children);  // 初始化子进程链表节点
    init_list_node(&p->ptnode);    // 初始化父进程链表节点
    init_schinfo(&p->schinfo);     // 初始化调度信息
    init_pgdir(&p->pgdir);         // 初始化空的进程的页目录
    void* pg = kalloc_page();
    memset(pg, 0, PAGE_SIZE);
    p->trapcontext = pg; // 中断上下文初始化为内核栈顶
    p->kcontext = pg + PAGE_SIZE - sizeof(KernelContext); // 初始化内核进程上下文
}

Proc* create_proc() // 创建进程
{
    Proc* proc = kalloc(sizeof(Proc));
    init_proc(proc);

    return proc;
}

void set_parent_to_this(Proc* proc) // 设置父进程为当前进程
{
    Proc* p = thisproc();
    acquire_spinlock(&proc->lock);
    proc->parent = p;
    release_spinlock(&proc->lock);
    acquire_spinlock(&p->lock);
    _insert_into_list(&p->children, &proc->ptnode);
    release_spinlock(&p->lock);
}

int start_proc(Proc* p, void (*entry)(u64), u64 arg) // 配置进程以启动加入调度
{
    acquire_spinlock(&p->lock);

    if (p->parent == NULL) {
        p->parent = &root_proc;
        acquire_spinlock(&root_proc.lock);
        _insert_into_list(&root_proc.children, &p->ptnode);
        release_spinlock(&root_proc.lock);
    }

    p->kcontext->x30 = (u64)proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;

    release_spinlock(&p->lock);

    activate_proc(p);

    return p->pid;
}

int wait(int* exitcode) // 等待子进程退出
{
    Proc* p = thisproc();
    acquire_spinlock(&p->lock);
    if (_empty_list(&p->children)) { // 如果没有子进程, 则返回 -1
        release_spinlock(&p->lock);
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
                kfree_page((void*)round_down((u64)proc->ucontext - 1, PAGE_SIZE));
                kfree(proc); // 释放进程结构体
                release_spinlock(&p->lock);
                return pid;
            }
            next_node = next_node->next;
        } while (next_node != end_node);

        release_spinlock(&p->lock);
        wait_sem(&p->childexit); // 等待子进程退出
        acquire_spinlock(&p->lock);
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
    release_spinlock(&p->lock);

    acquire_sched_lock();

    sched(ZOMBIE);
    free_pid(p->pid); // 释放 PID
    free_pgdir(&p->pgdir); // 释放进程的页目录

    PANIC();
}

static Proc* search_proc(Proc* root, int pid) // 递归遍历搜索进程树
{
    if (root->pid == pid) {
        return root;
    }
    
    ListNode* node = root->children.next;
    while (node != &root->children) {
        Proc* child = container_of(node, Proc, ptnode);
        Proc* result = search_proc(child, pid);
        if (result) return result;
        node = node->next;
    }
    
    return NULL;
}

int kill(int pid)
{
    if (pid <= NCPU + 1 || pid >= MAX_PID) return -1; // 检查 PID 是否有效

    Proc* target = search_proc(&root_proc, pid);
    if (target == NULL) {
        return -1; // 找不到进程
    }

    acquire_spinlock(&target->lock);
    target->killed = true; // 设置 killed 标志
    release_spinlock(&target->lock);

    alert_proc(target);

    return 0;
}
