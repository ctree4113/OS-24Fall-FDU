#pragma once
#include <fs/defines.h>

typedef struct { // 块读写函数接口
    void (*read)(usize block_no, u8* buffer);
    void (*write)(usize block_no, u8* buffer);
} BlockDevice;

extern BlockDevice block_device;

void init_block_device();

const SuperBlock* get_super_block();