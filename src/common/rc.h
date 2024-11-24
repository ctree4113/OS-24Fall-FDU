#pragma once

#include <common/defines.h>

typedef struct { // 引用计数
    isize count;
} RefCount;

void init_rc(volatile RefCount *);
void increment_rc(volatile RefCount *);
bool decrement_rc(volatile RefCount *);
