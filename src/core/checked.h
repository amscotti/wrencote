#ifndef WC_CORE_CHECKED_H
#define WC_CORE_CHECKED_H

#include <stdbool.h>

#if defined(__has_include)
#if __has_include(<stdckdint.h>)
#include <stdckdint.h>
#define WC_HAS_STDCKDINT 1
#endif
#endif

#ifndef WC_HAS_STDCKDINT
#define ckd_add(result, left, right) __builtin_add_overflow((left), (right), (result))
#define ckd_sub(result, left, right) __builtin_sub_overflow((left), (right), (result))
#define ckd_mul(result, left, right) __builtin_mul_overflow((left), (right), (result))
#endif

#endif
