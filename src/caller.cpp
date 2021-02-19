#include "caller.h"
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
        const uintptr_t* params;
        caller_impl(uintptr_t f_, const uintptr_t* params_) : f(reinterpret_cast<function_type<N>>(f_)), params(params_) {}
        uintptr_t call() const;
    };
    template <> uintptr_t caller_impl<0>::call() const { return f(); }
    template <> uintptr_t caller_impl<1>::call() const { return f(params[0]); }
    template <> uintptr_t caller_impl<2>::call() const { return f(params[0],params[1]); }
    template <> uintptr_t caller_impl<3>::call() const { return f(params[0],params[1],params[2]); }
    template <> uintptr_t caller_impl<4>::call() const { return f(params[0],params[1],params[2],params[3]); }
    template <> uintptr_t caller_impl<5>::call() const { return f(params[0],params[1],params[2],params[3],params[4]); }

    const caller* caller::create(uintptr_t f, const uintptr_t* params, size_t params_size) {
        switch (params_size) {
        case 0: 
        case 1: return new caller_impl<1>{f, params};
        case 2: return new caller_impl<2>{f, params};
        case 3: return new caller_impl<3>{f, params};
        case 4: return new caller_impl<4>{f, params};
        case 5: return new caller_impl<5>{f, params};
        default: return nullptr;
        }
    }
}
