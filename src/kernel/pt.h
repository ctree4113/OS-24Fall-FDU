#pragma once

#include <aarch64/mmu.h>

// 页目录，存储进程的用户态内存空间
struct pgdir {
    PTEntriesPtr pt; // 指向进程的用户页表的指针，如果进程是纯内核态进程，则为 NULL
    int level;
};

void init_pgdir(struct pgdir *pgdir);
WARN_RESULT PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc);
void free_pgdir(struct pgdir *pgdir);
void attach_pgdir(struct pgdir *pgdir);
