#ifndef __YHCHAOS_MACRO_H__
#define __YHCHAOS_MACRO_H__

#include <string.h>
#include <assert.h>
#include "log.h"
#include "util.h"

#if defined __GNUC__ || defined __llvm__
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率成立
#   define YHCHAOS_LIKELY(x)       __builtin_expect(!!(x), 1) //指示编译器条件的预期结果，以便编译器可以进行优化。
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率不成立
#   define YHCHAOS_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#else
#   define YHCHAOS_LIKELY(x)      (x)  //是将传入的条件 x 原样返回。
#   define YHCHAOS_UNLIKELY(x)      (x)
#endif

/// 断言宏封装
#define YHCHAOS_ASSERT(x) \
    if(YHCHAOS_UNLIKELY(!(x))) { \
        YHCHAOS_LOG_ERROR(YHCHAOS_LOG_ROOT()) << "ASSERTION: " #x \
            << "\nbacktrace:\n" \
            << yhchaos::BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

/// 断言宏封装
//w 是一个参数，用于传递附加的错误信息。在断言失败时，这个附加信息将一同被输出，以提供更多上下文信息，帮助定位问题。
#define YHCHAOS_ASSERT2(x, w) \
    if(YHCHAOS_UNLIKELY(!(x))) { \
        YHCHAOS_LOG_ERROR(YHCHAOS_LOG_ROOT()) << "ASSERTION: " #x \
            << "\n" << w \
            << "\nbacktrace:\n" \
            << yhchaos::BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

#endif
