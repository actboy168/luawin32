#include "caller.h"
#include <array>
#include <utility>

namespace win32 {
    template <typename>
    struct function_type_;
    template <size_t ...IS>
    struct function_type_<std::index_sequence<IS...>> {
        using type = uintptr_t (__stdcall *)(decltype(IS, uintptr_t())...);
    };
    template <size_t N>
    using function_type = typename function_type_<std::make_index_sequence<N>>::type;

    template <size_t N>
    struct caller_impl : public caller {
        function_type<N> f;
        std::array<uintptr_t, N> params;
        caller_impl(uintptr_t f_) : f(reinterpret_cast<function_type<N>>(f_)), params() {}
        void set(size_t i, uintptr_t v) { params[i] = v; }
        uintptr_t call() const;
    };
    template <> uintptr_t caller_impl<0>::call() const { return f(); }
    template <> uintptr_t caller_impl<1>::call() const { return f(params[0]); }
    template <> uintptr_t caller_impl<2>::call() const { return f(params[0],params[1]); }
    template <> uintptr_t caller_impl<3>::call() const { return f(params[0],params[1],params[2]); }
    template <> uintptr_t caller_impl<4>::call() const { return f(params[0],params[1],params[2],params[3]); }
    template <> uintptr_t caller_impl<5>::call() const { return f(params[0],params[1],params[2],params[3],params[4]); }

    caller* caller::create(uintptr_t f, size_t n) {
        switch (n) {
        case 0: 
        case 1: return new caller_impl<1>{f};
        case 2: return new caller_impl<2>{f};
        case 3: return new caller_impl<3>{f};
        case 4: return new caller_impl<4>{f};
        case 5: return new caller_impl<5>{f};
        default: return nullptr;
        }
    }
}
