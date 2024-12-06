#pragma once

extern "C" {
#include "common/defines.h"
}

struct MockLockConfig { // 模拟锁配置
    static constexpr bool SpinLockBlocksCPU = true;
    static constexpr bool SpinLockForbidsWait = false;
    static constexpr int WaitTimeoutSeconds = 10;
};

extern MockLockConfig mock_lock;
