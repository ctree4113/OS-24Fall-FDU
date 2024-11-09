#pragma once

#include <common/defines.h>
typedef unsigned long long u64;
#define PAGE_SIZE 4096

/* Memory region attributes */
#define MT_DEVICE_nGnRnE 0x0
#define MT_NORMAL 0x1
#define MT_NORMAL_NC 0x2
#define MT_DEVICE_nGnRnE_FLAGS 0x00
#define MT_NORMAL_FLAGS \
    0xFF /* Inner/Outer Write-Back Non-Transient RW-Allocate */
#define MT_NORMAL_NC_FLAGS 0x44 /* Inner/Outer Non-Cacheable */

#define SH_OUTER (2 << 8)
#define SH_INNER (3 << 8)

#define AF_USED (1 << 10)

#define PTE_NORMAL_NC ((MT_NORMAL_NC << 2) | AF_USED | SH_OUTER) // 普通非缓存页表项
#define PTE_NORMAL ((MT_NORMAL << 2) | AF_USED | SH_OUTER)       // 普通页表项
#define PTE_DEVICE ((MT_DEVICE_nGnRnE << 2) | AF_USED)           // 设备页表项

#define PTE_VALID 0x1 // 页表项有效

#define PTE_TABLE 0x3 // 页表项是页表
#define PTE_BLOCK 0x1 // 页表项是块
#define PTE_PAGE 0x3  // 页表项是页

#define PTE_KERNEL (0 << 6) // 内核页表项标记
#define PTE_USER (1 << 6)   // 用户页表项标记
#define PTE_RO (1 << 7)     // 只读页表项标记
#define PTE_RW (0 << 7)     // 可写页表项标记

#define PTE_KERNEL_DATA (PTE_KERNEL | PTE_NORMAL | PTE_BLOCK)   // 内核数据页表项
#define PTE_KERNEL_DEVICE (PTE_KERNEL | PTE_DEVICE | PTE_BLOCK) // 内核设备页表项
#define PTE_USER_DATA (PTE_USER | PTE_NORMAL | PTE_PAGE)        // 用户数据页表项

#define N_PTE_PER_TABLE 512 // 每个页表的页表项数

#define PTE_HIGH_NX (1LL << 54) // 高54位标记

#define KSPACE_MASK 0xFFFF000000000000 // 内核空间掩码

// convert kernel address into physical address.
#define K2P(addr) ((u64)(addr) - (KSPACE_MASK))

// convert physical address into kernel address.
#define P2K(addr) ((u64)(addr) + (KSPACE_MASK))

// convert any address into kernel address space.
#define KSPACE(addr) ((u64)(addr) | (KSPACE_MASK))

// conver any address into physical address space.
#define PSPACE(addr) ((u64)(addr) & (~KSPACE_MASK))

typedef u64 PTEntry;
typedef PTEntry PTEntries[N_PTE_PER_TABLE];
typedef PTEntry *PTEntriesPtr;

#define VA_OFFSET(va) ((u64)(va) & 0xFFF)              // 虚拟地址的低12位
#define PTE_ADDRESS(pte) ((pte) & ~0xFFFF000000000FFF) // 页表项的地址
#define PTE_FLAGS(pte) ((pte) & 0xFFFF000000000FFF)    // 页表项的标志
#define P2N(addr) (addr >> 12)                         // 物理地址转换为页表项索引
#define PAGE_BASE(addr) ((u64)addr & ~(PAGE_SIZE - 1)) // 页的基地址

#define VA_PART0(va) (((u64)(va) & 0xFF8000000000) >> 39) // 虚拟地址的高12位：第0级页表索引
#define VA_PART1(va) (((u64)(va) & 0x7FC0000000) >> 30)   // 虚拟地址的第12-21位：第1级页表索引
#define VA_PART2(va) (((u64)(va) & 0x3FE00000) >> 21)     // 虚拟地址的第22-30位：第2级页表索引
#define VA_PART3(va) (((u64)(va) & 0x1FF000) >> 12)       // 虚拟地址的第31-39位：第3级页表索引

#define VA_PART(va, level) (((u64)(va) >> (39 - 9 * level)) & 0x1FF) // 虚拟地址中标明某一层级页表索引的部分
