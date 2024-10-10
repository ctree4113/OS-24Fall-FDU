#include <aarch64/intrinsic.h>
#include <driver/base.h>
#include <driver/interrupt.h>
#include <driver/irq.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <driver/gicv3.h>

static InterruptHandler int_handler[NUM_IRQ_TYPES];

static void default_handler(u32 intid) // 原初中断处理函数
{
    printk("[Error CPU %lld]: Interrupt %d not implemented.", cpuid(), intid);
    PANIC();
}

void init_interrupt() // 中断处理函数初始化
{
    for (usize i = 0; i < NUM_IRQ_TYPES; i++) {
        int_handler[i] = default_handler;
    }
}

void set_interrupt_handler(InterruptType type, InterruptHandler handler) // 设置中断处理函数
{
    int_handler[type] = handler;
}

void interrupt_global_handler() // 全局中断处理函数
{
    u32 iar = gic_iar(); // 获取中断状态寄存器
    u32 intid = iar & 0x3ff; // 获取中断ID

    if (intid == 1023) { // 1023为伪中断
        printk("[Warning]: Spurious Interrupt.\n");
        return;
    }

    gic_eoi(iar);

    if (int_handler[intid])
        int_handler[intid](intid);
}
