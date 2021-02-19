#pragma once

#include <stdint.h>

namespace win32 {
    struct caller {
        virtual uintptr_t    call() const = 0;
        static const caller* create(uintptr_t f, const uintptr_t* params, size_t params_size);
    };
}
