#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/cpu.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <driver/gicv3.h>
#include <driver/timer.h>

static volatile bool boot_secondary_cpus = false;

void main()
{
    if (cpuid() == 0) {
        /* @todo: Clear BSS section.*/
        extern char edata[], end[];
        memset(edata, 0, (usize)(end - edata));

        /* initialize interrupt handler */
        init_interrupt(); // 初始化中断处理器

        uart_init();
        printk_init();


        gicv3_init(); // 初始化 GICV3 中断控制器
        gicv3_init_percpu(); // 初始化每个 CPU 的 GICV3 中断控制器

        init_clock_handler();

        /* initialize kernel memory allocator */
        kinit();

        /* initialize sched */
        init_sched(); // 初始化进程调度器

        /* initialize kernel proc */
        init_kproc(); // 初始化内核进程

        smp_init(); // 初始化 SMP 机制

        arch_fence();

        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
    } else {
        while (!boot_secondary_cpus)
            ;
        arch_fence();
        gicv3_init_percpu();
    }

    set_return_addr(idle_entry);
}
