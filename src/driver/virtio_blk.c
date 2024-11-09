#include <common/spinlock.h>
#include <driver/virtio.h>
#include <driver/interrupt.h>
#include <common/buf.h>
#include <common/sem.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

#define VIRTIO_MAGIC 0x74726976 // 魔数

struct disk { // 虚拟磁盘
    SpinLock lk;        // 保护虚拟磁盘的锁
    struct virtq virtq; // 虚拟磁盘的队列
} disk;

static void desc_init(struct virtq *virtq) // 初始化描述符
{
    for (int i = 0; i < NQUEUE; i++) {
        if (i != NQUEUE - 1) {
            virtq->desc[i].flags = VIRTQ_DESC_F_NEXT;
            virtq->desc[i].next = i + 1;
        }
    }
}

static int alloc_desc(struct virtq *virtq) // 分配描述符
{
    if (virtq->nfree == 0) {
        PANIC();
    }

    u16 d = virtq->free_head;
    if (virtq->desc[d].flags & VIRTQ_DESC_F_NEXT)
        virtq->free_head = virtq->desc[d].next;

    virtq->nfree--;

    return d;
}

static void free_desc(struct virtq *virtq, u16 n) // 释放描述符
{
    u16 head = n;
    int empty = 0;

    if (virtq->nfree == 0)
        empty = 1;

    while (virtq->nfree++, (virtq->desc[n].flags & VIRTQ_DESC_F_NEXT)) {
        n = virtq->desc[n].next;
    }

    virtq->desc[n].flags = VIRTQ_DESC_F_NEXT;
    if (!empty)
        virtq->desc[n].next = virtq->free_head;
    virtq->free_head = head;
}

int virtio_blk_rw(Buf *b) // 虚拟磁盘读写
{
    enum diskop op = DREAD; // 读操作
    if (b->flags & B_DIRTY) // 如果缓冲区被修改过，则需要进行写操作
        op = DWRITE;
    
    init_sem(&b->sem, 0); // 初始化信号量

    u64 sector = b->block_no; // 获取缓冲区所在的扇区
    struct virtio_blk_req_hdr hdr; // 虚拟磁盘请求头

    if (op == DREAD)
        hdr.type = VIRTIO_BLK_T_IN;
    else if (op == DWRITE)
        hdr.type = VIRTIO_BLK_T_OUT;
    else
        return -1;
    hdr.reserved = 0; // 保留字段
    hdr.sector = sector; // 扇区

    acquire_spinlock(&disk.lk);

    int d0 = alloc_desc(&disk.virtq); // 分配 d0 描述符
    if (d0 < 0)
        return -1;
    disk.virtq.desc[d0].addr = (u64)V2P(&hdr);     // 设置 d0 描述符的地址
    disk.virtq.desc[d0].len = sizeof(hdr);         // 设置 d0 描述符的长度
    disk.virtq.desc[d0].flags = VIRTQ_DESC_F_NEXT; // 设置 d0 描述符的标志

    int d1 = alloc_desc(&disk.virtq); // 分配 d1 描述符
    if (d1 < 0)
        return -1;
    disk.virtq.desc[d0].next = d1;                 // 设置 d0 描述符的下一个描述符
    disk.virtq.desc[d1].addr = (u64)V2P(b->data);  // 设置 d1 描述符的地址
    disk.virtq.desc[d1].len = 512;                 // 设置 d1 描述符的长度
    disk.virtq.desc[d1].flags = VIRTQ_DESC_F_NEXT; // 设置 d1 描述符的标志
    if (op == DREAD)
        disk.virtq.desc[d1].flags |= VIRTQ_DESC_F_WRITE; // 如果 op 为读操作，则设置 d1 描述符的标志为写操作

    int d2 = alloc_desc(&disk.virtq); // 分配 d2 描述符
    if (d2 < 0)
        return -1;
    disk.virtq.desc[d1].next = d2;                                    // 设置 d1 描述符的下一个描述符为 d2
    disk.virtq.desc[d2].addr = (u64)V2P(&disk.virtq.info[d0].status); // 设置 d2 描述符的地址
    disk.virtq.desc[d2].len = sizeof(disk.virtq.info[d0].status);     // 设置 d2 描述符的长度
    disk.virtq.desc[d2].flags = VIRTQ_DESC_F_WRITE;                   // 设置 d2 描述符的标志
    disk.virtq.desc[d2].next = 0;                                     // 设置 d2 描述符的下一个描述符为 0

    disk.virtq.avail->ring[disk.virtq.avail->idx % NQUEUE] = d0;      // 设置可用描述符的索引
    disk.virtq.avail->idx++;                                          // 增加可用描述符的索引

    disk.virtq.info[d0].buf = b;                                      // 设置 d0 描述符的缓冲区

    arch_fence(); // 内存屏障，确保内存操作完成
    REG(VIRTIO_REG_QUEUE_NOTIFY) = 0; // 通知虚拟磁盘
    arch_fence(); // 内存屏障

    /* LAB 4 TODO 1 BEGIN */

    b->disk = TRUE; // 设置缓冲区正在处理

    while (b->disk) {
        release_spinlock(&disk.lk); // 释放虚拟磁盘锁
        wait_sem(&b->sem);          // 等待缓冲区处理完成
        acquire_spinlock(&disk.lk); // 获取虚拟磁盘锁
    }
    
    /* LAB 4 TODO 1 END */

    disk.virtq.info[d0].done = 0; // 设置 d0 描述符的完成标志
    free_desc(&disk.virtq, d0);   // 释放 d0 描述符
    release_spinlock(&disk.lk);   // 释放虚拟磁盘锁
    return 0;
}

void virtio_blk_intr() // 虚拟磁盘中断处理
{
    acquire_spinlock(&disk.lk);

    u32 intr_status = REG(VIRTIO_REG_INTERRUPT_STATUS); // 设置中断状态
    REG(VIRTIO_REG_INTERRUPT_ACK) = intr_status & 0x3;  // 确认中断

    int d0;
    while (disk.virtq.last_used_idx != disk.virtq.used->idx) {
        d0 = disk.virtq.used->ring[disk.virtq.last_used_idx % NQUEUE].id; // 获取描述符环的头索引
        if (disk.virtq.info[d0].status != 0) {
            PANIC();
        }

        /* LAB 4 TODO 2 BEGIN */

        Buf *buf = disk.virtq.info[d0].buf; // 获取缓冲区
        buf->disk = FALSE;                  // 处理完成，释放缓冲区
        post_sem(&buf->sem);                // 释放信号量
        /* LAB 4 TODO 2 END */

        disk.virtq.info[d0].buf = NULL; // 释放缓冲区
        disk.virtq.last_used_idx++;     // 增加描述符环的索引
    }

    release_spinlock(&disk.lk);
}

static int virtq_init(struct virtq *vq) // 初始化虚拟队列
{
    memset(vq, 0, sizeof(*vq));

    vq->desc = kalloc_page();  // 分配描述符页
    vq->avail = kalloc_page(); // 分配待处理页
    vq->used = kalloc_page();  // 分配已处理页

    memset(vq->desc, 0, 4096);
    memset(vq->avail, 0, 4096);
    memset(vq->used, 0, 4096);

    if (!vq->desc || !vq->avail || !vq->used) {
        PANIC();
    }
    vq->nfree = NQUEUE; // 空闲描述符数量
    desc_init(vq);      // 初始化描述符

    return 0;
}

void virtio_init() // 初始化
{
    if (REG(VIRTIO_REG_MAGICVALUE) != VIRTIO_MAGIC ||
        REG(VIRTIO_REG_VERSION) != 2 || REG(VIRTIO_REG_DEVICE_ID) != 2) {
        printk("[Virtio]: Device not found.");
        PANIC();
    }

    /* Reset the device. */
    REG(VIRTIO_REG_STATUS) = 0;

    u32 status = 0;

    /* Set the ACKNOWLEDGE status bit: the guest OS has noticed the device. */
    status |= DEV_STATUS_ACKNOWLEDGE;
    REG(VIRTIO_REG_STATUS) = status;

    /* Set the DRIVER status bit: the guest OS knows how to drive the device. */
    status |= DEV_STATUS_DRIVER;
    REG(VIRTIO_REG_STATUS) = status;

    /* Read device feature bits, and write the subset of feature bits understood by the OS and driver to the device. */
    REG(VIRTIO_REG_DEVICE_FEATURES_SEL) = 0;
    REG(VIRTIO_REG_DRIVER_FEATURES_SEL) = 0;

    u32 features = REG(VIRTIO_REG_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_SEG_MAX);
    features &= ~(1 << VIRTIO_BLK_F_GEOMETRY);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_BLK_SIZE);
    features &= ~(1 << VIRTIO_BLK_F_FLUSH);
    features &= ~(1 << VIRTIO_BLK_F_TOPOLOGY);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    REG(VIRTIO_REG_DRIVER_FEATURES) = features;

    status |= DEV_STATUS_FEATURES_OK;
    REG(VIRTIO_REG_STATUS) = status;

    arch_fence();
    status = REG(VIRTIO_REG_STATUS);
    arch_fence();
    if (!(status & DEV_STATUS_FEATURES_OK)) {
        PANIC();
    }

    virtq_init(&disk.virtq);

    int qmax = REG(VIRTIO_REG_QUEUE_NUM_MAX);
    if (qmax < NQUEUE) {
        printk("[Virtio]: Too many queues.");
        PANIC();
    }

    REG(VIRTIO_REG_QUEUE_SEL) = 0;
    REG(VIRTIO_REG_QUEUE_NUM) = NQUEUE;

    u64 phy_desc = V2P(disk.virtq.desc);
    REG(VIRTIO_REG_QUEUE_DESC_LOW) = LO(phy_desc);
    REG(VIRTIO_REG_QUEUE_DESC_HIGH) = HI(phy_desc);

    u64 phy_avail = V2P(disk.virtq.avail);
    REG(VIRTIO_REG_QUEUE_DRIVER_LOW) = LO(phy_avail);
    REG(VIRTIO_REG_QUEUE_DRIVER_HIGH) = HI(phy_avail);
    u64 phy_used = V2P(disk.virtq.used);

    REG(VIRTIO_REG_QUEUE_DEVICE_LOW) = LO(phy_used);
    REG(VIRTIO_REG_QUEUE_DEVICE_HIGH) = HI(phy_used);

    arch_fence();

    REG(VIRTIO_REG_QUEUE_READY) = 1;
    status |= DEV_STATUS_DRIVER_OK;
    REG(VIRTIO_REG_STATUS) = status;

    arch_fence();

    set_interrupt_handler(VIRTIO_BLK_IRQ, virtio_blk_intr); // 设置中断处理跳转地址
    init_spinlock(&disk.lk);                                // 初始化虚拟磁盘锁
}
