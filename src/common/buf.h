#pragma once

#include <common/defines.h>
#include <common/sem.h>

#define BSIZE 512
#define B_VALID 0x2 // Buffer has been read from disk.
#define B_DIRTY 0x4 // Buffer needs to be written to disk.

typedef struct { // 缓冲区结构体
    int flags;      // 缓冲区标志
    u8 data[BSIZE]; // 缓冲区数据
    u32 block_no;   // 缓冲区块号

    /* @todo: It depends on you to add other necessary elements. */
    bool disk;      // 虚拟磁盘是否正在处理缓冲区
    Semaphore sem;  // 信号量
} Buf;
