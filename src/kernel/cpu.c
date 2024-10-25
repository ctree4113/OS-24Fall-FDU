#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <driver/timer.h>
#include <driver/clock.h>

struct cpu cpus[NCPU];

static bool __timer_cmp(rb_node lnode, rb_node rnode) // 定时器比较函数
{
    i64 d = container_of(lnode, struct timer, _node)->_key -
            container_of(rnode, struct timer, _node)->_key;
    if (d < 0)
        return true;
    if (d == 0)
        return lnode < rnode; // 键值相等则比较地址
    return false;
}

static void __timer_set_clock() // 更新定时器
{
    auto node = _rb_first(&cpus[cpuid()].timer);
    if (!node) {
        // printk("cpu %lld set clock 1000, no timer left\n", cpuid());
        reset_clock(10);
        return;
    }
    auto t1 = container_of(node, struct timer, _node)->_key;
    auto t0 = get_timestamp_ms();
    if (t1 <= t0)
        reset_clock(0);
    else
        reset_clock(t1 - t0);
    // printk("cpu %lld set clock %lld\n", cpuid(), t1 - t0);
}

static void timer_clock_handler() // 定时器中断处理函数
{
    reset_clock(10); // 重置定时器为 10 ms
    // printk("cpu %lld aha, timestamp ms: %lld\n", cpuid(), get_timestamp_ms());
    while (1) {
        auto node = _rb_first(&cpus[cpuid()].timer);
        if (!node)
            break;
        auto timer = container_of(node, struct timer, _node);
        if (get_timestamp_ms() < timer->_key)
            break;
        cancel_cpu_timer(timer);
        timer->triggered = true;
        timer->handler(timer);
    }
}

void init_clock_handler() // 初始化定时器中断处理函数
{
    set_clock_handler(&timer_clock_handler);
}

static struct timer hello_timer[4];

static void hello(struct timer *t)
{
    printk("CPU %lld: living\n", cpuid());
    t->data++;
    set_cpu_timer(&hello_timer[cpuid()]);
}

void set_cpu_timer(struct timer *timer) // 启动 CPU 定时器节点
{
    if (_rb_lookup(&timer->_node, &cpus[cpuid()].timer, __timer_cmp))
        return;
    timer->triggered = false;
    timer->_key = get_timestamp_ms() + timer->elapse;
    ASSERT(0 == _rb_insert(&timer->_node, &cpus[cpuid()].timer, __timer_cmp));
    __timer_set_clock();
}

void cancel_cpu_timer(struct timer *timer) // 取消 CPU 定时器节点
{
    ASSERT(!timer->triggered);
    _rb_erase(&timer->_node, &cpus[cpuid()].timer);
    __timer_set_clock();
}

void set_cpu_on() // 设置 CPU 上线
{
    ASSERT(!_arch_disable_trap());
    // disable the lower-half address to prevent stupid errors
    extern PTEntries invalid_pt;
    arch_set_ttbr0(K2P(&invalid_pt)); // 设置页表
    extern char exception_vector[]; // 异常向量表
    arch_set_vbar(exception_vector); // 设置异常向量表
    arch_reset_esr(); // 重置异常状态寄存器
    init_clock(); // 初始化时钟
    cpus[cpuid()].online = true; // 设置 CPU 上线
    printk("CPU %lld: hello\n", cpuid()); // 打印欢迎信息
    hello_timer[cpuid()].elapse = 5000; // 设置定时器间隔
    hello_timer[cpuid()].handler = hello; // 设置定时器处理函数
    set_cpu_timer(&hello_timer[cpuid()]); // 启动定时器
}

void set_cpu_off() // 设置 CPU 关闭
{
    _arch_disable_trap(); // 关闭中断
    cpus[cpuid()].online = false; // 设置 CPU 离线
    printk("CPU %lld: stopped\n", cpuid());
}
