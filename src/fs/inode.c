#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/rc.c>

static const SuperBlock* sblock; // 超级块
static const BlockCache* cache; // 块缓存
static SpinLock lock; // 自旋锁
static ListNode head; // 内存索引链表

static INLINE usize to_block_no(usize inode_no) { // 计算 inode 所在的块号
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

static INLINE InodeEntry* get_entry(Block* block, usize inode_no) { // 获取 inode 的磁盘索引项
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

static INLINE u32* get_addrs(Block* block) { // 获取间接块的地址
    return ((IndirectBlock*)block->data)->addrs;
}

void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) { // 初始化 inode 树
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

static void init_inode(Inode* inode) { // 初始化内存中的 inode
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

static usize inode_alloc(OpContext* ctx, InodeType type) { // 分配新的 inode
    ASSERT(type != INODE_INVALID);
    
    for (u32 i = ROOT_INODE_NO; i < sblock->num_inodes; i++) {
        Block* block = cache->acquire(to_block_no(i));
        InodeEntry* entry = get_entry(block, i);
        if (entry->type == INODE_INVALID) { // 找到空闲的 inode 位
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, block);
            cache->release(block);
            return i;
        }
        cache->release(block);
    }
    
    PANIC(); // 如果找不到空闲的 inode 位，则 panic
}

static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    wait_sem(&inode->lock);
}

static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    post_sem(&inode->lock);
}

static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) { // 同步 inode
    ASSERT(inode->rc.count > 0);
    ASSERT((do_write == true && inode->valid == false) == false); // 如果需要写回但 inode 无效，则 panic
    if (do_write == false && inode->valid == false) { // 如果不需要写（懒读取）回且 inode 尚且无效，则从磁盘读取
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);
        inode->entry = *entry;
        cache->release(block);
        inode->valid = true;
        ASSERT(inode->entry.type != INODE_INVALID);
    }
    else { // 将内存中的 inode 写回磁盘
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);
        *entry = inode->entry;
        cache->sync(ctx, block);
        cache->release(block);
    }
}

static Inode* inode_get(usize inode_no) { // 获取 inode
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    
    Inode* inode;
    
    for (ListNode* p = head.next; p != &head; p = p->next) {
        inode = container_of(p, Inode, node);
        if(inode->inode_no == inode_no) { // 如果索引项已经在内存中, 则增加其引用计数
            increment_rc(&inode->rc);
            inode_lock(inode);
            release_spinlock(&lock);
            inode_unlock(inode); // 加锁解锁，确保线程安全
            return inode;
        }
    }

     // 如果索引项不在内存中，创建新的 inode
    inode = kalloc(sizeof(Inode));
    init_inode(inode);
    inode->inode_no = inode_no;
    increment_rc(&inode->rc); // 为新创建的 inode 增加引用计数
    _insert_into_list(&head, &inode->node);
    inode_lock(inode); // 在锁的保护下实现同步
    release_spinlock(&lock);
    inode_sync(NULL, inode, false);
    inode_unlock(inode); 

    ASSERT(inode->entry.type != INODE_INVALID); // 确保新创建的 inode 是有效的
    return inode;
}

static void inode_clear(OpContext* ctx, Inode* inode) { // 清除 inode
    InodeEntry* entry = &inode->entry;
    for (int i = 0; i < INODE_NUM_DIRECT; i++) { // 释放所有直接块
        if (entry->addrs[i]) {
            cache->free(ctx, entry->addrs[i]);
            entry->addrs[i] = 0;
        }
    }
    
    if (entry->indirect) { // 释放所有间接块引用的数据块
        Block* indirect = cache->acquire(entry->indirect);
        u32* addrs = get_addrs(indirect);
        for (long unsigned i = 0; i < INODE_NUM_INDIRECT; i++) {
            if (addrs[i])
                cache->free(ctx, addrs[i]);
        }
        cache->release(indirect);
        cache->free(ctx, entry->indirect);
        entry->indirect = 0;
    }
    
    entry->num_bytes = 0;
    inode_sync(ctx, inode, true); // 将更新索引项写回硬盘
}

static Inode* inode_share(Inode* inode) { // 共享 inode
    acquire_spinlock(&lock);
    increment_rc(&inode->rc); // 引用计数增加
    release_spinlock(&lock);
    return inode;
}

static void inode_put(OpContext* ctx, Inode* inode) { // 释放 inode
    acquire_spinlock(&lock);
    
    if (inode->rc.count == 1 && inode->valid && inode->entry.num_links == 0) {  // 如果这是最后一个引用且没有硬链接，则清除 inode
        _detach_from_list(&inode->node);

        inode_lock(inode);
        release_spinlock(&lock); // 先释放自旋锁
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, true);
        inode->valid = false;
        inode_unlock(inode);

        acquire_spinlock(&lock); // 重新获取自旋锁，再释放内存
        kfree(inode);
        release_spinlock(&lock);
        return;
    }
    
    decrement_rc(&inode->rc); // 减少引用计数
    release_spinlock(&lock);    
}

static usize inode_map(OpContext* ctx,
                      Inode* inode,
                      usize offset,
                      bool* modified) { // 获取 inode 的偏移量
    ASSERT(offset < INODE_MAX_BYTES);
    ASSERT(modified != NULL);  // 确保 modified 指针有效
    
    InodeEntry* entry = &inode->entry;
    *modified = false;
    
    if (offset < INODE_NUM_DIRECT) { // 处理直接块
        if (entry->addrs[offset] == 0 && ctx != NULL) { // 如果直接块不存在，则分配新块
            entry->addrs[offset] = cache->alloc(ctx);
            *modified = true;
        }
        return entry->addrs[offset];
    }
    
    // 处理间接块
    offset -= INODE_NUM_DIRECT;
    if (offset < INODE_NUM_INDIRECT) {
        if (entry->indirect == 0 && ctx != NULL) { // 如果间接块不存在，则分配新块
            entry->indirect = cache->alloc(ctx);
            *modified = true;
        }
        
        // 获取并处理间接块
        Block* indirect = cache->acquire(entry->indirect);
        u32* addrs = get_addrs(indirect);
        usize block_no = 0;  // 预设返回值
        
        if (addrs[offset] == 0 && ctx != NULL) { // 如果间接块的偏移量对应的块不存在，则分配新块
            addrs[offset] = cache->alloc(ctx);
            cache->sync(ctx, indirect);
            *modified = true;
        }
        
        block_no = addrs[offset];
        cache->release(indirect);
        return block_no;
    }
    
    PANIC();  // 越界访问，panic
}

static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) { // 读取 inode
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    if (count == 0) return 0; // 处理空读取的情况

    // 按块读取数据
    usize bytes_read = 0;  // 已读取的字节数
    usize curr_offset = offset;  // 当前读取位置
    
    for (usize block_idx = offset/BLOCK_SIZE; block_idx <= (end-1)/BLOCK_SIZE; block_idx++) {
        usize bytes_to_read = MIN(end - curr_offset, (block_idx + 1) * BLOCK_SIZE - curr_offset); // 计算当前块需要读取的字节数
        bool modified; // 是否修改
        usize block_no = inode_map(NULL, inode, block_idx, &modified); // 获取数据块
        if (block_no == 0) continue;  // 跳过空块
        Block* block = cache->acquire(block_no);
        memmove(dest + bytes_read, block->data + (curr_offset % BLOCK_SIZE), bytes_to_read); // 读取数据
        cache->release(block);
        
        curr_offset += bytes_to_read;
        bytes_read += bytes_to_read;
    }
    
    return bytes_read;
}

static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) { // 写入 inode
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // 按块写入数据
    usize bytes_written = 0;    // 已写入的字节数
    usize curr_offset = offset; // 当前写入位置
    for (usize block_idx = offset/BLOCK_SIZE; block_idx <= (end-1)/BLOCK_SIZE; block_idx++) {
        usize bytes_to_write = MIN(end - curr_offset, (block_idx + 1) * BLOCK_SIZE - curr_offset); // 计算当前块需要写入的字节数
        bool block_allocated; // 标记是否分配了新块
        usize block_no = inode_map(ctx, inode, block_idx, &block_allocated); // 获取数据块
        ASSERT(block_no != 0); // 写操作必须有有效的数据块
        Block* block = cache->acquire(block_no);
        memmove(block->data + (curr_offset % BLOCK_SIZE), src + bytes_written, bytes_to_write); // 写入数据
        cache->sync(ctx, block); // 同步到磁盘
        cache->release(block);
        
        curr_offset += bytes_to_write; // 更新写入进度
        bytes_written += bytes_to_write;
    }
    
    if (end > entry->num_bytes) { // 更新文件大小（如果需要）
        entry->num_bytes = end;
        inode_sync(ctx, inode, true); // 同步 inode 元数据
    }
    
    return bytes_written;
}

static usize inode_lookup(Inode* inode, const char* name, usize* index) { // 查找 inode
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    for (usize offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)) {
        DirEntry dir_entry;
        usize bytes_read = inode_read(inode, (u8*)&dir_entry, offset, sizeof(DirEntry)); // 读取目录项
        ASSERT(bytes_read == sizeof(DirEntry)); // 确保读取完整的目录项
        
        if (dir_entry.inode_no != 0 && strncmp(name, dir_entry.name, FILE_NAME_MAX_LENGTH) == 0) { // 如果找到匹配的文件名
            if (index != NULL) *index = offset; // 如果需要，返回目录项的偏移量
            return dir_entry.inode_no; // 返回找到的 inode 号
        }
    }
    
    return 0; // 未找到文件
}

static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) { // 插入 inode
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    
    if (inode_lookup(inode, name, NULL)) return -1; // 检查是否已存在同名文件
        
    DirEntry dir_entry; // 目录项
    usize offset;       // 目录项偏移量
    
    for (offset = 0; offset < entry->num_bytes; offset += sizeof(dir_entry)) { // 查找空闲目录项
        if (inode_read(inode, (u8*)&dir_entry, offset, sizeof(dir_entry)) != sizeof(dir_entry)) break; // 读取目录项
        if (dir_entry.inode_no == 0) break; // 找到空闲目录项
    }
    
    strncpy(dir_entry.name, name, FILE_NAME_MAX_LENGTH); // 复制文件名
    dir_entry.inode_no = inode_no; // 设置 inode 号
    if (inode_write(ctx, inode, (u8*)&dir_entry, offset, sizeof(dir_entry)) != sizeof(dir_entry)) return -1; // 写入目录项
        
    return offset / sizeof(dir_entry); // 返回目录项偏移量
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    ASSERT(index % sizeof(DirEntry) == 0);  // 确保索引对齐
    ASSERT(index < inode->entry.num_bytes);  // 确保索引在有效范围内
    
    DirEntry empty_entry = {0};
    inode_write(ctx, inode, (u8*)&empty_entry, index, sizeof(DirEntry)); // 写入空目录项，实现删除
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
