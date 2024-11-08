#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>

volatile bool panic_flag; // 是否 panic

NO_RETURN void idle_entry() // main 函数最终跳转至此
{
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap
        {
            arch_wfi(); // 等待中断
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry() // 内核入口
{
    printk("Hello world! (Core %lld)\n", cpuid());
    // proc_test();
    // vm_test();
    // user_proc_test();
    io_test();

    /* LAB 4 TODO 3 BEGIN */
    
    /* LAB 4 TODO 3 END */

    while (1)
        yield();
}

NO_INLINE NO_RETURN void _panic(const char *file, int line) // 内核 panic，停止所有 CPU
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) { // 等待所有 CPU 停止
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}