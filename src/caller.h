#pragma once

#include <stdint.h>

namespace win32 {
    struct caller {
        virtual uintptr_t call() const = 0;
        virtual void      set(size_t i, uintptr_t v) = 0;
        static caller*    create(uintptr_t f, size_t n);
    };
}
