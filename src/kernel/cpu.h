#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>

#define NCPU 4

struct sched {
    Proc* proc;      // 当前执行的进程
    Proc* idle_proc; // 初始化的 idle 进程
    Proc* last_proc; // 转为 idle 进程前执行的进程
};

struct cpu {
    bool online;           // 是否在线
    struct rb_root_ timer; // 计时器
    struct sched sched;    // 调度信息
};

extern struct cpu cpus[NCPU];
#define thiscpu (&cpus[cpuid()])

struct timer { // 定时器
    bool triggered;
    int elapse;
    u64 _key;
    struct rb_node_ _node;
    void (*handler)(struct timer *);
    u64 data;
};

void init_clock_handler();

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);