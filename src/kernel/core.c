#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/virtio.h>

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
    
    Buf MBR;
    MBR.flags = 0;
    MBR.block_no = 0;
    virtio_blk_rw(&MBR); // 解析 MBR
    u32 *p2_LBA = (u32 *)(MBR.data + 0x1CE + 0x8); // 获得第⼆分区起始块的 LBA
    u32 *p2_sectors = (u32 *)(MBR.data + 0x1CE + 0xC); // 获得第⼆分区大小
    printk("Partition 2 LBA: %d, Sectors: %d\n", *p2_LBA, *p2_sectors);
    
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