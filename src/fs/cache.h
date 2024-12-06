#pragma once
#include <common/list.h>
#include <common/sem.h>
#include <fs/block_device.h>
#include <fs/defines.h>

#define OP_MAX_NUM_BLOCKS 10  // 单次可写入的最大块数
#define EVICTION_THRESHOLD 20 // 块缓存阈值

typedef struct {
    ListNode node;       // 链表节点
    usize block_no;      // 块号
    usize acquire;       // 引用计数
    bool pinned;         // 是否被锁定
    SleepLock lock;      // 睡眠锁
    bool valid;          // 有效位
    u8 data[BLOCK_SIZE]; // 数据
} Block; // 缓存块

typedef struct {
    usize rm; // 事务剩余可写块数
    usize ts; // 时间戳
} OpContext; // 事务上下文

typedef struct {
    usize (*get_num_cached_blocks)();             // 已缓存块数
    Block* (*acquire)(usize block_no);            // 锁定缓存块
    void (*release)(Block* block);                // 释放缓存块
    void (*begin_op)(OpContext* ctx);             // 开始事务
    void (*sync)(OpContext* ctx, Block* block);   // 写回日志
    void (*end_op)(OpContext* ctx);               // 结束事务
    usize (*alloc)(OpContext* ctx);               // 分配磁盘块
    void (*free)(OpContext* ctx, usize block_no); // 释放磁盘块
} BlockCache; // 块缓存函数接口
extern BlockCache bcache;

void init_bcache(const SuperBlock* sblock, const BlockDevice* device); // 初始化块缓存