#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/defines.h>
#include <common/string.h>
#include <kernel/printk.h>

RefCount kalloc_page_cnt;
extern char end[];

static struct FreePage* free_page_head; // 空闲页链表头
static SpinLock free_page_lock; // 空闲页锁
static const u16 slab_sizes[] = { 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048 }; // slab 页中对象的大小
static const int num_slab_sizes = sizeof(slab_sizes) / sizeof(slab_sizes[0]); // slab 页中对象的种类数

struct FreePage {
    struct FreePage* next;
};

struct SlabPage { // 一个 slab 页
    u16 obj_size; // 对象大小
    u16 obj_cnt; // 对象数量
    u16 free_obj_head_offset; // 空闲对象头偏移
    struct SlabPage* next;
};

static struct {
    SpinLock lock; // 锁
    struct SlabPage* partial; // 部分空闲的 slab 页
    struct SlabPage* full; // 完全空闲的 slab 页
} slab_allocators[32];


void kinit()
{
    init_rc(&kalloc_page_cnt);

    free_page_head = (struct FreePage*)round_up((u64)end, PAGE_SIZE);

    // 初始化页空闲链表
    auto p = (u64)free_page_head;
    auto page = (struct FreePage*)p;
    for (; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        page = (struct FreePage*)p;
        page->next = (struct FreePage*)(p + PAGE_SIZE);
    }
    page->next = NULL;

    // 初始化Slab分配器
    for (int i = 0; i < num_slab_sizes; i++) {
        init_spinlock(&slab_allocators[i].lock);
        slab_allocators[i].partial = NULL;
        slab_allocators[i].full = NULL;
    }

    init_spinlock(&free_page_lock);
}

void* kalloc_page()
{
    acquire_spinlock(&free_page_lock);

    void* page = free_page_head;
    if (page)
        free_page_head = free_page_head->next;

    release_spinlock(&free_page_lock);
    
    if (page) {
        memset(page, 0, PAGE_SIZE);
        increment_rc(&kalloc_page_cnt);
    }

    return page;
}

void kfree_page(void* p)
{
    acquire_spinlock(&free_page_lock);

    ((struct FreePage*)p)->next = free_page_head;
    free_page_head = p;

    release_spinlock(&free_page_lock);
    
    decrement_rc(&kalloc_page_cnt);
}

static int get_slab_index(unsigned long long size) // 获取某个分配大小应对应的 slab 索引
{
    for (int i = 0; i < num_slab_sizes; i++) {
        if (size <= slab_sizes[i])
            return i;
    }
    return -1;
}

void* kalloc(unsigned long long size)
{
    int index = get_slab_index(size); 
    if (index == -1)
        return kalloc_page();

    acquire_spinlock(&slab_allocators[index].lock);

    struct SlabPage* page = slab_allocators[index].partial;

    if (page == NULL) { // 如果没有部分空闲的 slab 页，则分配一个新页
        page = kalloc_page();
        if (page == NULL) {
            release_spinlock(&slab_allocators[index].lock);
            return NULL;
        }
        page->obj_size = slab_sizes[index];
        page->obj_cnt = 0;
        page->free_obj_head_offset = sizeof(struct SlabPage);
        page->next = NULL;

        u16 obj_offset = page->free_obj_head_offset;
        for (; obj_offset <= PAGE_SIZE - page->obj_size; obj_offset += page->obj_size) { // 初始化空闲对象链表
            *(u16*)((u64)page + obj_offset) = obj_offset + page->obj_size;
        }
        *(u16*)((u64)page + obj_offset - page->obj_size) = 0;

        slab_allocators[index].partial = page; // 设置为部分空闲的 slab 页
    }

    void* obj = (void*)((u64)page + page->free_obj_head_offset); // 获取对象地址
    page->free_obj_head_offset = *(u16*)obj; // 更新空闲对象头偏移
    page->obj_cnt++; // 增加对象数量

    if (page->free_obj_head_offset == 0) { // 如果空闲对象头偏移为0, 说明该页完全空闲，移动到full链表
        slab_allocators[index].partial = page->next;
        page->next = slab_allocators[index].full;
        slab_allocators[index].full = page;
    }

    release_spinlock(&slab_allocators[index].lock);

    return obj;
}

void kfree(void* ptr)
{
    struct SlabPage* page = (struct SlabPage*)((u64)ptr & ~(PAGE_SIZE - 1)); // 获取 slab 页地址
    int index = get_slab_index(page->obj_size);

    if (index == -1) { // 如果该对象不属于任何 slab 页，则释放页
        kfree_page(ptr);
        return;
    }

    // 如果该对象属于某个 slab 页，则释放对象
    acquire_spinlock(&slab_allocators[index].lock);

    *(u16*)ptr = page->free_obj_head_offset;
    page->free_obj_head_offset = (u16)((u64)ptr - (u64)page); // 更新空闲对象头偏移
    page->obj_cnt--;

    if (page->obj_cnt == 0) { // 如果该页没有对象，则释放页
        struct SlabPage** p = &slab_allocators[index].partial;
        while (*p && *p != page)
            p = &(*p)->next;
        if (*p)
            *p = page->next;
        
        p = &slab_allocators[index].full;
        while (*p && *p != page)
            p = &(*p)->next;
        if (*p)
            *p = page->next;

        release_spinlock(&slab_allocators[index].lock);

        kfree_page(page);
    } else if (page->obj_cnt == PAGE_SIZE / page->obj_size - 1) { // 如果该页的对象数量恰好等于该页可容纳的最大对象数量减 1，则将该页从 full 链表移动到 partial 链表
        struct SlabPage** p = &slab_allocators[index].full;
        while (*p && *p != page)
            p = &(*p)->next;
        if (*p) {
            *p = page->next;
            page->next = slab_allocators[index].partial;
            slab_allocators[index].partial = page;
        }

        release_spinlock(&slab_allocators[index].lock);
    } else {
        release_spinlock(&slab_allocators[index].lock);
    }
}