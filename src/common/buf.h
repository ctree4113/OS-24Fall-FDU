#pragma once

#include <common/defines.h>
#include <common/sem.h>

#define BSIZE 512   // 缓冲区大小
#define B_VALID 0x2 // 缓冲区已从磁盘读取
#define B_DIRTY 0x4 // 缓冲区需要写入磁盘
#define B_FREE  0x0 // 缓冲区空闲

typedef struct { // 缓冲区结构体
    int flags;      // 缓冲区标志
    u8 data[BSIZE]; // 缓冲区数据
    u32 block_no;   // 磁盘块号

    /* @todo: It depends on you to add other necessary elements. */
    bool disk;      // 虚拟磁盘是否正在处理缓冲区
    Semaphore sem;  // 信号量
} Buf;
