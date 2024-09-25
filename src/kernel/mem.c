#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/defines.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>

RefCount kalloc_page_cnt;
extern char end[];

#define CPU_CACHE_SIZE 32 // 每个CPU缓存最多存储的对象数量
#define NUM_CPUS 4        // 系统中的CPU数量
#define NUM_SLAB_SIZES 8  // slab大小类别的数量

struct FreePage {
    struct FreePage* next;
};

struct SlabPage {
    u16 obj_size; // 对象大小
    u16 obj_cnt;  // 已分配对象数量
    u16 free_obj_head_offset; // 空闲对象链表头部偏移
    struct SlabPage* next;
};

// 确保每个CPU缓存在不同的缓存行，避免伪共享
struct CPUCache {
    void* objs[CPU_CACHE_SIZE]; // 16 * 8 = 128字节
    int count;                  // 4字节
} __attribute__((aligned(64))); // 确保64字节对齐


static const u16 slab_sizes[NUM_SLAB_SIZES] = { 8, 16, 32, 64, 128, 256, 512, 1024 }; 

static struct {
    SpinLock lock;
    struct SlabPage* partial; // 部分使用的slab页
    struct SlabPage* full;    // 完全使用的slab页
    struct CPUCache cpu_caches[NUM_CPUS]; // 每个CPU的本地缓存
} slab_allocators[NUM_SLAB_SIZES];

static struct FreePage* free_page_head; // 空闲页链表头
static SpinLock free_page_lock; // 空闲页锁

void kinit() {
    init_rc(&kalloc_page_cnt);

    // 初始化空闲页链表
    free_page_head = (struct FreePage*)round_up((u64)end, PAGE_SIZE);
    auto p = (u64)free_page_head;
    auto page = (struct FreePage*)p;
    for (; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        page = (struct FreePage*)p;
        page->next = (struct FreePage*)(p + PAGE_SIZE);
    }
    page->next = NULL;

    // 初始化slab分配器和CPU缓存
    for (int i = 0; i < NUM_SLAB_SIZES; i++) {
        init_spinlock(&slab_allocators[i].lock);
        slab_allocators[i].partial = NULL;
        slab_allocators[i].full = NULL;
        
        // 初始化每个CPU的缓存
        for (int cpu = 0; cpu < NUM_CPUS; cpu++) {
            slab_allocators[i].cpu_caches[cpu].count = 0;
        }
    }

    init_spinlock(&free_page_lock);
}

void* kalloc_page() {
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

void kfree_page(void* p) {
    acquire_spinlock(&free_page_lock);
    ((struct FreePage*)p)->next = free_page_head;
    free_page_head = p;
    release_spinlock(&free_page_lock);
    decrement_rc(&kalloc_page_cnt);
}

static int get_slab_index(unsigned long long size) {
    for (int i = 0; i < NUM_SLAB_SIZES; i++) {
        if (size <= slab_sizes[i])
            return i;
    }
    return -1;
}

static struct SlabPage* new_slab_page(int index) {
    struct SlabPage* page = kalloc_page();
    if (!page) return NULL;

    page->obj_size = slab_sizes[index];
    page->obj_cnt = 0;
    page->free_obj_head_offset = sizeof(struct SlabPage);
    page->next = NULL;

    // 构建空闲对象链表
    u16 obj_offset = page->free_obj_head_offset;
    while (obj_offset + page->obj_size <= PAGE_SIZE) {
        *(u16*)((u64)page + obj_offset) = obj_offset + page->obj_size;
        obj_offset += page->obj_size;
    }
    *(u16*)((u64)page + obj_offset - page->obj_size) = 0;

    return page;
}

void* kalloc(unsigned long long size) {
    int index = get_slab_index(size);
    if (index == -1)
        return kalloc_page();

    // 首先尝试从CPU缓存分配
    int cpu_id = cpuid();
    struct CPUCache* cache = &slab_allocators[index].cpu_caches[cpu_id];
    
    // 无锁快速路径：从CPU缓存分配
    if (cache->count > 0) {
        return cache->objs[--cache->count];
    }

    // 慢路径：从slab分配器分配
    acquire_spinlock(&slab_allocators[index].lock);
    struct SlabPage* page = slab_allocators[index].partial;

    if (page == NULL) {
        page = new_slab_page(index);
        if (page == NULL) {
            release_spinlock(&slab_allocators[index].lock);
            return NULL;
        }
        slab_allocators[index].partial = page;
    }

    // 从slab页分配对象
    void* obj = (void*)((u64)page + page->free_obj_head_offset);
    page->free_obj_head_offset = *(u16*)obj;
    page->obj_cnt++;

    // 如果页面已满，移到full链表
    if (page->free_obj_head_offset == 0) {
        slab_allocators[index].partial = page->next;
        page->next = slab_allocators[index].full;
        slab_allocators[index].full = page;
    }

    release_spinlock(&slab_allocators[index].lock);
    return obj;
}

void kfree(void* ptr) {
    if (ptr == NULL) return;

    struct SlabPage* page = (struct SlabPage*)((u64)ptr & ~(PAGE_SIZE - 1));
    int index = get_slab_index(page->obj_size);

    if (index == -1) {
        kfree_page(ptr);
        return;
    }

    // 首先尝试放入CPU缓存
    int cpu_id = cpuid();
    struct CPUCache* cache = &slab_allocators[index].cpu_caches[cpu_id];
    
    // 无锁快速路径：放入CPU缓存
    if (cache->count < CPU_CACHE_SIZE) {
        cache->objs[cache->count++] = ptr;
        return;
    }

    // 慢路径：归还到slab
    acquire_spinlock(&slab_allocators[index].lock);

    // 将对象加入空闲链表
    *(u16*)ptr = page->free_obj_head_offset;
    page->free_obj_head_offset = (u16)((u64)ptr - (u64)page);
    page->obj_cnt--;

    if (page->obj_cnt == 0) {
        // 页面完全空闲，释放它
        struct SlabPage** p = &slab_allocators[index].partial;
        while (*p && *p != page) p = &(*p)->next;
        if (*p) *p = page->next;
        else {
            p = &slab_allocators[index].full;
            while (*p && *p != page) p = &(*p)->next;
            if (*p) *p = page->next;
        }
        release_spinlock(&slab_allocators[index].lock);
        kfree_page(page);
    } else if (page->obj_cnt == PAGE_SIZE / page->obj_size - 1) {
        // 从full移动到partial
        struct SlabPage** p = &slab_allocators[index].full;
        while (*p && *p != page) p = &(*p)->next;
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