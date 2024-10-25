#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void *syscall_table[NR_SYSCALL] = { // 系统调用表
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context) // 系统调用入口
{
    u64 id = context->x8; // 系统调用 ID, 存储在用户进程上下文的 x8 寄存器中
    if (id >= NR_SYSCALL) PANIC(); // 检查系统调用 ID 是否超出范围
    if(syscall_table[id])
        context->x0 = ((u64 (*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
            context->x0, context->x1, context->x2, context->x3, context->x4, context->x5);
    else PANIC();
}

#pragma GCC diagnostic pop