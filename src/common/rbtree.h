#pragma once
#include "common/defines.h"

struct rb_node_ { // 红黑树节点
    unsigned long __rb_parent_color;
    struct rb_node_ *rb_right;
    struct rb_node_ *rb_left;
} __attribute__((aligned(sizeof(long))));

typedef struct rb_node_ *rb_node;
struct rb_root_ { // 红黑树根节点
    rb_node rb_node;
};
typedef struct rb_root_ *rb_root;

/* NOTE:You should add lock when use */
int _rb_insert(rb_node node, rb_root root,
               bool (*cmp)(rb_node lnode, rb_node rnode)); // 插入节点
void _rb_erase(rb_node node, rb_root root); // 删除节点
rb_node _rb_lookup(rb_node node, rb_root rt,
                   bool (*cmp)(rb_node lnode, rb_node rnode)); // 查找节点
rb_node _rb_first(rb_root root); // 查找第一个节点
