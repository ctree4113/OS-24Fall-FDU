#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <common/sem.h>

static const SuperBlock* sblock;  // 超级块
static const BlockDevice* device; // 块读写接口

static ListNode head; // 缓存链环头
static SpinLock lock; // 缓存链表锁

struct Log {
    SpinLock lock;     // 日志锁
    Semaphore sem;     // 日志信号量
    usize count;       // 当前事务数量
    bool iscommitting; // 是否正在提交
} log;

static LogHeader header; // 日志头

static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&header);
}

static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&header);
}

static void init_block(Block* block) // 块初始化
{
    block->block_no = 0;
    block->acquire = 0;
    block->pinned = false;
    block->valid = false;
    init_list_node(&block->node);
    init_sleeplock(&block->lock);
    memset(block->data, 0, sizeof(block->data));
}

static usize get_num_cached_blocks() // 回当前cache中块的个数
{
    usize count = 0;
    for (ListNode* node = head.next; node != &head; node = node->next)
        count++;
    return count;
}

static Block* cache_get(usize block_no) // 获取缓存块
{
    acquire_spinlock(&lock);
    usize num_cached_blocks = get_num_cached_blocks();
    if (num_cached_blocks >= EVICTION_THRESHOLD) // 移除多余的缓存块
        for (ListNode* node = head.prev; node != &head;) {
            ListNode* prev = node->prev;
            Block* block = container_of(node, Block, node);
            if (block->acquire == 0 && block->pinned == false) { // 如果没有aquire任何块且未绑定日志
                _detach_from_list(node);
                kfree(block);
                num_cached_blocks--;
                if (num_cached_blocks < EVICTION_THRESHOLD)
                    break;
            }
            node = prev;
        }

    for (ListNode* node = head.next; node != &head; node = node->next) { // 寻找缓存块
        Block* block = container_of(node, Block, node);
        if (block->block_no == block_no) { // 硬盘块号匹配
            block->acquire++;
            _detach_from_list(&block->node);
            _insert_into_list(&head, &block->node);
            release_spinlock(&lock);
            return block;
        }
    }

    // 如果没有找到对应的缓存块，则插入新的缓存块
    Block* block = kalloc(sizeof(Block));
    init_block(block);
    block->block_no = block_no;
    block->acquire = 1;
    _insert_into_list(&head, &block->node);
    release_spinlock(&lock);
    return block;
}

static Block* cache_acquire(usize block_no) // 锁定缓存块
{
    Block* block = cache_get(block_no);
    acquire_sleeplock(&block->lock);
    if (block->valid == false) { // 如果是无效块，则从磁盘读入数据
        device_read(block);
        block->valid = true;
    }

    return block;
}

static void cache_release(Block* block) // 释放缓存块
{
    release_sleeplock(&block->lock);
    acquire_spinlock(&lock);
    ASSERT(block->acquire > 0);
    block->acquire--;
    release_spinlock(&lock);
}

static void write_des(bool isrecovering)
{
    for (usize num = 0; num < header.num_blocks; num++) {
        Block* lblock = cache_acquire((sblock->log_start + 1) + num);
        Block* dblock = cache_acquire(header.block_no[num]);
        memcpy(dblock->data, lblock->data, BLOCK_SIZE);
        device_write(dblock);
        if (isrecovering == false) dblock->pinned = false; // 不是重启恢复,则不绑定日志
        cache_release(lblock);
        cache_release(dblock);
    }
}

static void write_log()
{
    for (usize num = 0; num < header.num_blocks; num++) {
        Block* lbuf = cache_acquire((sblock->log_start + 1) + num);
        Block* dbuf = cache_acquire(header.block_no[num]);
        memcpy(lbuf->data, dbuf->data, BLOCK_SIZE);
        device_write(lbuf);
        cache_release(lbuf);
        cache_release(dbuf);
    }
}

static void commit()
{
    if (header.num_blocks > 0) {
        write_log(); 
        write_header();
        write_des(false);
        header.num_blocks = 0;
        write_header();
    }
}

void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) // 文件系统初始化
{
    sblock = _sblock;
    device = _device;
    init_list_node(&head);
    init_spinlock(&lock);
    init_spinlock(&log.lock);
    init_sem(&log.sem, 0);
    log.count = 0;
    log.iscommitting = false;

    read_header();
    write_des(true);
    header.num_blocks = 0;
    write_header();
}

static void cache_begin_op(OpContext* ctx) // 事务开始
{
    ASSERT(ctx != NULL);
    acquire_spinlock(&log.lock);

    while (1) {
        if (log.iscommitting == true) sleep_sem(&log.sem, &log.lock); // 等待提交
        else if (header.num_blocks + (log.count + 1) * OP_MAX_NUM_BLOCKS >= MIN(sblock->num_log_blocks, LOG_MAX_SIZE)) sleep_sem(&log.sem, &log.lock); // 等待日志空间
        else {
            log.count++;                 // 增加当前事务数
            ctx->rm = OP_MAX_NUM_BLOCKS; // 事务剩余可写块数设为最大
            break;
        }
    }

    release_spinlock(&log.lock);
}

static void cache_sync(OpContext* ctx, Block* block) // 写回日志
{
    if (ctx == NULL) { // 如果不在任何事务中，直接写回
        device_write(block);
        return;
    }

    acquire_spinlock(&log.lock);
    ASSERT(header.num_blocks + 1 < sblock->num_log_blocks);
    ASSERT(log.count >= 1);
    bool iflog = false;

    for (usize num = 0; num < header.num_blocks; num++)
        if (header.block_no[num] == block->block_no) {
            iflog = true;
            break;
        }

    if (iflog == false) { // 如果不在日志中, 则添加日志
        ASSERT(ctx->rm >= 1);
        ctx->rm--; // 减少剩余的可写块数
        block->pinned = true;
        header.block_no[header.num_blocks] = block->block_no;
        header.num_blocks++;
    }

    release_spinlock(&log.lock);
}

static void cache_end_op(OpContext* ctx) // 事务结束
{
    ASSERT(ctx != NULL);
    ctx->rm = 0; // 将剩余的可写块数清零
    int ifcommit = false;

    acquire_spinlock(&log.lock);

    log.count--;
    ASSERT(log.iscommitting == false);
    if (log.count != 0) wakeup_sem(&log.sem); // 唤醒等待日志空间的事务
    else {
        ifcommit = true;
        log.iscommitting = true;
    }

    release_spinlock(&log.lock);

    if (ifcommit == true) {
        commit();
        acquire_spinlock(&log.lock);
        log.iscommitting = false;
        wakeup_sem(&log.sem); // 唤醒等待提交结束的事务
        release_spinlock(&log.lock);
    }
}

static usize cache_alloc(OpContext* ctx) // 分配磁盘块
{
    for (usize p = 0; p < sblock->num_blocks; p += BIT_PER_BLOCK) {
        Block* block = cache_acquire(BIT_BLOCK_NO(p, sblock));
        for (usize b = 0; b < BIT_PER_BLOCK && p + b < sblock->num_blocks; b++) {
            u8 mask = 1 << (b % 8);
            if ((block->data[b / 8] & mask) == 0) { // 如果当前位是空闲的，则分配
                block->data[b / 8] |= mask;
                cache_sync(ctx, block);
                cache_release(block);
                usize block_no = p + b;
                Block* block = cache_acquire(block_no);
                memset(block->data, 0, BLOCK_SIZE);
                cache_sync(ctx, block);
                cache_release(block);
                return block_no;
            }
        }
        cache_release(block);
    }

    PANIC(); // 磁盘应当有空闲块可分配
}

static void cache_free(OpContext* ctx, usize block_no) // 释放磁盘块
{
    usize b = block_no % BIT_PER_BLOCK;
    u8 mask = 1 << (b % 8);
    Block* block = cache_acquire(BIT_BLOCK_NO(block_no, sblock));
    block->data[b / 8] &= ~mask;
    cache_sync(ctx, block);
    cache_release(block);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};