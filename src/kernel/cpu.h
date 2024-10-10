#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>

#define NCPU 4

struct sched {
    Proc* proc; // 当前执行的进程,
    Proc* idle_proc; // 初始化的 idle 进程
};

struct cpu {
    bool online; // 是否在线
    struct rb_root_ timer; // 计时器
    struct sched sched; // 调度信息
};

extern struct cpu cpus[NCPU];
#define thiscpu (&cpus[cpuid()])

void set_cpu_on();
void set_cpu_off();
