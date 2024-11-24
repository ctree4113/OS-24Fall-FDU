#include <aarch64/intrinsic.h>

void delay_us(u64 n) // 微秒延迟
{
    u64 freq = get_clock_frequency(); // 获取系统时钟频率
    u64 end = get_timestamp(), now;
    end += freq / 1000000 * n; // 计算结束时间

    do {
        now = get_timestamp();
    } while (now <= end); // 等待直到达到目标时间戳
}

void smp_init() // 多处理器初始化
{
    // 依次启动1-3号CPU核心
    psci_cpu_on(1, SECONDARY_CORE_ENTRY);
    psci_cpu_on(2, SECONDARY_CORE_ENTRY);
    psci_cpu_on(3, SECONDARY_CORE_ENTRY);
}
