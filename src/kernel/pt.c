#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc) // 找到对应于指定虚拟地址的页表项，alloc 指示是否允许分配
{
    u64 pte_index = va & KSPACE_MASK; // 获取虚拟地址的页表项索引
    ASSERT(pte_index == 0 || pte_index == KSPACE_MASK); // 确保地址要么在用户空间，要么在内核空间

    if (pgdir->pt == NULL) { // 如果页表为空，则分配并初始化一个页表
        if (alloc) {
            pgdir->pt = (PTEntriesPtr)kalloc_page();
        } else {
            return NULL;
        }
        memset(pgdir->pt, 0, PAGE_SIZE);
    }

    PTEntriesPtr pt = pgdir->pt;

    // 遍历0-3三级页表
    for (int level = 0; level <= 2; ++level) {
        PTEntriesPtr pte = &pt[VA_PART(va, level)]; // 获取当前级页表项
        if (*pte & PTE_VALID) { // 如果当前页表项有效，则获取下一级页表
            pt = (PTEntriesPtr)P2K(PTE_ADDRESS(*pte));
            continue;
        }
        
        // 如果页表项无效且不允许分配，则返回 NULL
        if (!alloc) return NULL;

        // 如果页表项无效但允许分配，则分配并初始化新的下一级页表
        pt = (PTEntriesPtr)kalloc_page();
        memset(pt, 0, PAGE_SIZE);
        *pte = K2P(pt) | PTE_VALID | PTE_TABLE | PTE_USER | PTE_RW;
    }

    return &pt[VA_PART3(va)]; // 返回指向第3级页表项的指针
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = NULL;
    pgdir->level = 0;
}

void free_pgdir(struct pgdir *pgdir) // 释放页目录中页表占用的页面空间，但并不释放页表所引用的页面
{
    if (pgdir->pt == NULL)
        return;

    if (pgdir->level <= 2) {
        for (int i = 0; i < N_PTE_PER_TABLE; i++) { // 遍历页表的所有页表项
            auto pte = pgdir->pt[i];
            if (pte & PTE_VALID) {
                struct pgdir pgdir_child;
                pgdir_child.pt = (PTEntriesPtr)P2K(PTE_ADDRESS(pte));
                pgdir_child.level = pgdir->level + 1;
                free_pgdir(&pgdir_child); // 递归释放下一级页表
            }
        }
    }

    // 释放当前页表页
    kfree_page(pgdir->pt);
    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir *pgdir) // 将页目录附加到当前进程
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}
