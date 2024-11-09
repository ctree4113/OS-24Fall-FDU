#pragma once

#include <common/defines.h>
#include <driver/base.h>
#include <common/buf.h>

#define NQUEUE 8 // 虚拟磁盘的队列数量

#define VIRTIO_REG_MAGICVALUE (VIRTIO0 + 0x00)          // 魔数
#define VIRTIO_REG_VERSION (VIRTIO0 + 0x04)             // 版本
#define VIRTIO_REG_DEVICE_ID (VIRTIO0 + 0x08)           // 设备 ID
#define VIRTIO_REG_VENDOR_ID (VIRTIO0 + 0x0c)           // 子系统 ID
#define VIRTIO_REG_DEVICE_FEATURES (VIRTIO0 + 0x10)     // 设备特性
#define VIRTIO_REG_DEVICE_FEATURES_SEL (VIRTIO0 + 0x14) // 设备特性选择
#define VIRTIO_REG_DRIVER_FEATURES (VIRTIO0 + 0x20)     // 驱动特性
#define VIRTIO_REG_DRIVER_FEATURES_SEL (VIRTIO0 + 0x24) // 驱动特性选择
#define VIRTIO_REG_QUEUE_SEL (VIRTIO0 + 0x30)           // 队列选择
#define VIRTIO_REG_QUEUE_NUM_MAX (VIRTIO0 + 0x34)       // 最大队列数量
#define VIRTIO_REG_QUEUE_NUM (VIRTIO0 + 0x38)           // 队列数量
#define VIRTIO_REG_QUEUE_READY (VIRTIO0 + 0x44)         // 队列就绪
#define VIRTIO_REG_QUEUE_NOTIFY (VIRTIO0 + 0x50)        // 通知
#define VIRTIO_REG_INTERRUPT_STATUS (VIRTIO0 + 0x60)    // 中断状态
#define VIRTIO_REG_INTERRUPT_ACK (VIRTIO0 + 0x64)       // 中断确认
#define VIRTIO_REG_STATUS (VIRTIO0 + 0x70)              // 状态
#define VIRTIO_REG_QUEUE_DESC_LOW (VIRTIO0 + 0x80)      // 队列描述符低地址
#define VIRTIO_REG_QUEUE_DESC_HIGH (VIRTIO0 + 0x84)     // 队列描述符高地址
#define VIRTIO_REG_QUEUE_DRIVER_LOW (VIRTIO0 + 0x90)    // 驱动描述符低地址
#define VIRTIO_REG_QUEUE_DRIVER_HIGH (VIRTIO0 + 0x94)   // 驱动描述符高地址
#define VIRTIO_REG_QUEUE_DEVICE_LOW (VIRTIO0 + 0xa0)    // 设备描述符低地址
#define VIRTIO_REG_QUEUE_DEVICE_HIGH (VIRTIO0 + 0xa4)   // 设备描述符高地址
#define VIRTIO_REG_CONFIG_GENERATION (VIRTIO0 + 0xfc)   // 配置生成
#define VIRTIO_REG_CONFIG (VIRTIO0 + 0x100)             // 配置

#define DEV_STATUS_ACKNOWLEDGE 1  // 确认虚拟设备
#define DEV_STATUS_DRIVER 2       // 可以驱动虚拟设备
#define DEV_STATUS_FAILED 128     // 失败
#define DEV_STATUS_FEATURES_OK 8  // 特性确认
#define DEV_STATUS_DRIVER_OK 4    // 驱动确认
#define DEV_STATUS_NEEDS_RESET 64 // 需要重置

#define VIRTIO_BLK_F_SIZE_MAX 1          // 最大大小
#define VIRTIO_BLK_F_SEG_MAX 2           // 最大段
#define VIRTIO_BLK_F_GEOMETRY 4          // 几何
#define VIRTIO_BLK_F_RO 5                // 只读
#define VIRTIO_BLK_F_BLK_SIZE 6          // 块大小
#define VIRTIO_BLK_F_FLUSH 9             // 刷新
#define VIRTIO_BLK_F_TOPOLOGY 10         // 拓扑
#define VIRTIO_BLK_F_CONFIG_WCE 11       // 配置写回
#define VIRTIO_BLK_F_DISCARD 13          // 丢弃
#define VIRTIO_BLK_F_WRITE_ZEROES 14     // 写零
#define VIRTIO_F_ANY_LAYOUT 27           // 任意布局
#define VIRTIO_RING_F_INDIRECT_DESC 28   // 间接描述符
#define VIRTIO_RING_F_EVENT_IDX 29       // 事件索引

#define VIRTIO_BLK_S_OK 0     // 成功
#define VIRTIO_BLK_S_IOERR 1  // IO 错误
#define VIRTIO_BLK_S_UNSUPP 2 // 不支持

#define VIRTQ_DESC_F_NEXT 1     // 下一个描述符
#define VIRTQ_DESC_F_WRITE 2    // 写操作
#define VIRTQ_DESC_F_INDIRECT 4 // 间接描述符
struct virtq_desc { // 虚拟磁盘的描述符
    u64 addr;       // 地址
    u32 len;        // 长度
    u16 flags;      // 标志位
    u16 next;       // 下一个描述符
} __attribute__((packed, aligned(16)));

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1 // 没有中断
struct virtq_avail { // 待处理描述符
    u16 flags;        // 标志位，始终为0（没有中断）
    u16 idx;          // 索引
    u16 ring[NQUEUE]; // 描述符的环形队列
} __attribute__((packed, aligned(2)));

struct virtq_used_elem {
    u32 id;   // 描述符的ID
    u32 len;  // 描述符的长度
} __attribute__((packed));

#define VIRTQ_USED_F_NO_NOTIFY 1 // 没有通知
struct virtq_used { // 已处理描述符
    u16 flags;                           // 标志位，始终为0（没有通知）
    u16 idx;                             // 索引
    struct virtq_used_elem ring[NQUEUE]; // 描述符的环形队列
} __attribute__((packed, aligned(4)));

struct virtq { // 虚拟磁盘的队列
    struct virtq_desc *desc;   // 描述符
    struct virtq_avail *avail; // 待处理描述符
    struct virtq_used *used;   // 已处理描述符
    u16 free_head;             // 空闲描述符链表
    u16 nfree;
    u16 last_used_idx;         // 最后一个已使用描述符的索引

    struct { // 正在处理的描述符
        volatile u8 status; // 状态
        volatile u8 done;   // 完成
        Buf *buf;            // 缓冲区
    } info[NQUEUE];
};

#define VIRTIO_BLK_T_IN 0            // 输入
#define VIRTIO_BLK_T_OUT 1           // 输出
#define VIRTIO_BLK_T_FLUSH 4         // 刷新
#define VIRTIO_BLK_T_DISCARD 11      // 丢弃
#define VIRTIO_BLK_T_WRITE_ZEROES 13 // 写零
struct virtio_blk_req_hdr { // 虚拟磁盘请求头
    u32 type;     // 类型
    u32 reserved; // 保留
    u64 sector;   // 扇区
} __attribute__((packed));

enum diskop { // 虚拟磁盘操作
    DREAD,  // 读
    DWRITE, // 写
};

void virtio_init(void);    // 虚拟磁盘初始化
void virtio_blk_intr();    // 虚拟磁盘中断
int virtio_blk_rw(Buf *b); // 虚拟磁盘读写
