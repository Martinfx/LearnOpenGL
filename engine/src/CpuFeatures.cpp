#include "engine/CpuFeatures.h"

namespace engine {

bool checkCpuFeatures(std::string* missing)
{
    std::string list;
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    auto need = [&](bool has, const char* name) {
        if (!has)
            list += list.empty() ? name : std::string(", ") + name;
    };
    // Each check is compiled in only when the compiler was allowed to use that set.
#ifdef __SSE4_1__
    need(__builtin_cpu_supports("sse4.1"), "SSE4.1");
#endif
#ifdef __SSE4_2__
    need(__builtin_cpu_supports("sse4.2"), "SSE4.2");
#endif
#ifdef __POPCNT__
    need(__builtin_cpu_supports("popcnt"), "POPCNT");
#endif
#ifdef __AVX__
    need(__builtin_cpu_supports("avx"), "AVX");
#endif
#ifdef __AVX2__
    need(__builtin_cpu_supports("avx2"), "AVX2");
#endif
#ifdef __FMA__
    need(__builtin_cpu_supports("fma"), "FMA");
#endif
#ifdef __BMI__
    need(__builtin_cpu_supports("bmi"), "BMI");
#endif
#ifdef __AVX512F__
    need(__builtin_cpu_supports("avx512f"), "AVX-512F");
#endif
#ifdef __AVX512VL__
    need(__builtin_cpu_supports("avx512vl"), "AVX-512VL");
#endif
#ifdef __AVX512DQ__
    need(__builtin_cpu_supports("avx512dq"), "AVX-512DQ");
#endif
#endif
    if (missing)
        *missing = list;
    return list.empty();
}

} // namespace engine
