#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/base.h>
#include <driver/clock.h>
#include <driver/interrupt.h>
#include <kernel/printk.h>
#include <driver/timer.h>

static struct { // 定时器中断处理函数
    ClockHandler handler;
} clock;

void init_clock() // 初始化定时器
{
    // reserve one second for the first time.
    enable_timer();  // 启用
    reset_clock(10); // 设置等待 10 ms
}

void reset_clock(u64 interval_ms) // 重置定时器
{
    u64 interval_clk = interval_ms * get_clock_frequency() / 1000; // 将时间的毫秒数转换为 CPU 时钟周期数
    ASSERT(interval_clk <= 0x7fffffff);
    set_cntv_tval_el0(interval_clk);
}

void set_clock_handler(ClockHandler handler) // 设置定时器中断处理函数
{
    clock.handler = handler;
    set_interrupt_handler(TIMER_IRQ, invoke_clock_handler);
}

void invoke_clock_handler() // 调用定时器中断处理函数
{
    if (!clock.handler)
        PANIC();
    clock.handler();
}

u64 get_timestamp_ms() // 获取当前时间戳
{
    return get_timestamp() * 1000 / get_clock_frequency();
}
