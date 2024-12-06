#pragma once
#include <common/defines.h>

#define FSSIZE 1000    // 文件系统总块数
#define BLOCK_SIZE 512 // 块大小

typedef struct {
    u32 num_blocks;      // 总块数
    u32 num_data_blocks; // 数据块数
    u32 num_inodes;      // 索引数
    u32 num_log_blocks;  // 日志块数
    u32 log_start;       // 第一个日志块号
    u32 inode_start;     // 第一个索引块号
    u32 bitmap_start;    // 第一个位图块号
} SuperBlock; // 超级块

#define ROOT_INODE_NO 1                                   // 根目录索引编号
#define INODE_PER_BLOCK (BLOCK_SIZE / sizeof(InodeEntry)) // 每块最大索引数


#define BIT_PER_BLOCK (BLOCK_SIZE * 8)                               // 每块的最大位图数
#define BIT_BLOCK_NO(b, sb) ((b) / BIT_PER_BLOCK + sb->bitmap_start) // 位图块号

#define INODE_NUM_DIRECT 12                                      // 直接块数
#define INODE_NUM_INDIRECT (BLOCK_SIZE / sizeof(u32))            // 间接块数
#define INODE_MAX_BLOCKS (INODE_NUM_DIRECT + INODE_NUM_INDIRECT) // 最大总块数
#define INODE_MAX_BYTES (INODE_MAX_BLOCKS * BLOCK_SIZE)          // 最大文件大小

typedef u16 InodeType; // 索引类型
enum {
    INODE_INVALID = 0,   // 空闲
    INODE_DIRECTORY = 1, // 目录
    INODE_REGULAR = 2,   // 文件
    INODE_DEVICE = 3,    // 设备
};

typedef struct dinode {
    InodeType type;              // 索引类型
    u16 major;                   // 主设备号
    u16 minor;                   // 次设备号
    u16 num_links;               // 硬链接数
    u32 num_bytes;               // 文件大小
    u32 addrs[INODE_NUM_DIRECT]; // 直接块块号
    u32 indirect;                // 间接块块号
} InodeEntry; // 磁盘索引项，记录 inode 的元数据在磁盘上的位置

typedef struct {
    u32 addrs[INODE_NUM_INDIRECT]; // 间接块块号
} IndirectBlock; // 间接块

#define FILE_NAME_MAX_LENGTH 14 // 文件名最大长度

typedef struct dirent {
    u16 inode_no;                    // 索引号
    char name[FILE_NAME_MAX_LENGTH]; // 文件名
} DirEntry; // 目录下的文件

#define LOG_MAX_SIZE ((BLOCK_SIZE - sizeof(usize)) / sizeof(usize)) // 日志头块最大记录数

typedef struct {
    usize num_blocks;             // 记录数
    usize block_no[LOG_MAX_SIZE]; // 记录块号
} LogHeader; // 日志头
