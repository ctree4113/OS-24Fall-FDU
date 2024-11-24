#include <aarch64/trap.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>

void trap_global_handler(UserContext *context) // 全局异常处理函数
{
    Proc* p = thisproc();
    p->ucontext = context;

    u64 esr = arch_get_esr();     // 获取异常状态寄存器
    u64 ec = esr >> ESR_EC_SHIFT; // 获取异常代码
    u64 iss = esr & ESR_ISS_MASK; // 获取异常 ID
    u64 ir = esr & ESR_IR_SHIFT;   // 获取异常是否为中断

    (void)iss;

    arch_reset_esr(); // 重置异常状态寄存器

    switch (ec) {
        case ESR_EC_UNKNOWN: { // 未知异常
            if (ir)
                PANIC();
            else
                interrupt_global_handler();
        } break;

        case ESR_EC_SVC64: {
            syscall_entry(context);
        } break;

        case ESR_EC_IABORT_EL0: // el0 指令异常
        case ESR_EC_IABORT_EL1: // el1 指令异常
        case ESR_EC_DABORT_EL0: // el0 数据异常

        case ESR_EC_DABORT_EL1: { // el1 数据异常
            u64 far = arch_get_far();
            printk("Page fault at address 0x%llx\n", far);
            PANIC();
        } break;

        default: {
            printk("Unknwon exception %llu\n", ec);
            PANIC();
        }
    }

    if (p->killed && (context->spsr_el1 & 0xF) == 0) // 如果进程被终止且当前模式为返回到用户态，则退出进程
        exit(-1);
}

NO_RETURN void trap_error_handler(u64 type) // 原初异常处理函数
{
    printk("Unknown trap type %llu\n", type);
    PANIC();
}
