#include <driver/virtio.h>
#include <fs/block_device.h>
#include <common/string.h>

BlockDevice block_device; // 块读写函数接口

static void sd_read(usize block_no, u8* buffer)
{
    Buf b;
    b.block_no = (u32)block_no;
    b.flags = B_FREE;
    virtio_blk_rw(&b);
    memcpy(buffer, b.data, BLOCK_SIZE);
}

static void sd_write(usize block_no, u8* buffer)
{
    Buf b;
    b.block_no = (u32)block_no;
    b.flags = B_DIRTY | B_VALID;
    memcpy(b.data, buffer, BLOCK_SIZE);
    virtio_blk_rw(&b);
}

void init_block_device()
{
    block_device.read = sd_read;
    block_device.write = sd_write;
}

static u8 sblock_data[BLOCK_SIZE]; // 超级块数据

const SuperBlock* get_super_block() { return (const SuperBlock*)sblock_data; }