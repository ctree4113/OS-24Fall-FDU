#include <common/defines.h>
#include <driver/interrupt.h>
#include <aarch64/intrinsic.h>
#include <kernel/printk.h>

#define CNTV_CTL_ENABLE (1 << 0)
#define CNTV_CTL_IMASK (1 << 1)
#define CNTV_CTL_ISTATUS (1 << 2)

void enable_timer() // 启用定时器
{
    u64 c = get_cntv_ctl_el0();
    c |= CNTV_CTL_ENABLE;
    c &= ~CNTV_CTL_IMASK; // 清除定时器中断屏蔽位
    set_cntv_ctl_el0(c);
}

void disable_timer() // 禁用
{
    u64 c = get_cntv_ctl_el0();
    c &= ~CNTV_CTL_ENABLE;
    c |= CNTV_CTL_IMASK;
    set_cntv_ctl_el0(c);
}

bool timer_enabled() // 判断定时器是否被启用
{
    u64 c = get_cntv_ctl_el0();
    return c & 1;
}