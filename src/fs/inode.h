#pragma once
#include <common/list.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <fs/cache.h>
#include <fs/defines.h>

#define ROOT_INODE_NO 1 // 根目录的 inode 号

typedef struct {
    SleepLock lock;   // 睡眠锁
    RefCount rc;      // 引用计数
    ListNode node;    // 链表节点
    usize inode_no;   // inode 索引号
    bool valid;       // 有效位
    InodeEntry entry; // 磁盘索引项，内存中的副本
} Inode;

typedef struct {
    Inode* root; // 根目录
    usize (*alloc)(OpContext* ctx, InodeType type); // 分配 inode
    void (*lock)(Inode* inode); // 锁定 inode
    void (*unlock)(Inode* inode); // 解锁 inode
    void (*sync)(OpContext* ctx, Inode* inode, bool do_write); // 同步 inode
    Inode* (*get)(usize inode_no); // 获取 inode
    void (*clear)(OpContext* ctx, Inode* inode); // 清空 inode 的内容
    Inode* (*share)(Inode* inode); // 共享 inode
    void (*put)(OpContext* ctx, Inode* inode); // 释放 inode
    usize (*read)(Inode* inode, u8* dest, usize offset, usize count); // 读取 inode 的内容
    usize (*write)(OpContext* ctx,
                   Inode* inode,
                   u8* src,
                   usize offset,
                   usize count); // 写入 inode 的内容
    usize (*lookup)(Inode* inode, const char* name, usize* index); // 查找目录下的文件
    usize (*insert)(OpContext* ctx,
                    Inode* inode,
                    const char* name,
                    usize inode_no); // 插入目录项

    void (*remove)(OpContext* ctx, Inode* inode, usize index); // 删除目录项
} InodeTree; // inode 层函数接口

extern InodeTree inodes; // 全局 inode 层实例

void init_inodes(const SuperBlock* sblock, const BlockCache* cache); // 初始化 inode 层
