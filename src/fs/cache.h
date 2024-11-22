#pragma once
#include <common/list.h>
#include <common/sem.h>
#include <fs/block_device.h>
#include <fs/defines.h>

#define OP_MAX_NUM_BLOCKS 10  // 单次可写入的最大块数
#define EVICTION_THRESHOLD 20 // 块缓存阈值

typedef struct { // 缓存块
    ListNode node;
    usize block_no;
    usize acquire;
    bool pinned;
    SleepLock lock;
    bool valid; 
    u8 data[BLOCK_SIZE];
} Block;

typedef struct { // 当前日志状态
    usize rm; // 事务剩余可写块数
    usize ts; // 时间戳
} OpContext;

typedef struct { // 缓存块函数接口
    usize (*get_num_cached_blocks)();             // 已缓存块数
    Block* (*acquire)(usize block_no);            // 锁定缓存块
    void (*release)(Block* block);                // 释放缓存块
    void (*begin_op)(OpContext* ctx);             // 开始事务
    void (*sync)(OpContext* ctx, Block* block);   // 写回日志
    void (*end_op)(OpContext* ctx);               // 结束事务
    usize (*alloc)(OpContext* ctx);               // 分配磁盘块
    void (*free)(OpContext* ctx, usize block_no); // 释放磁盘块
} BlockCache;
extern BlockCache bcache;

void init_bcache(const SuperBlock* sblock, const BlockDevice* device); // 初始化块缓存