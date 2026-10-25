#ifndef WC_CORE_ASSERT_H
#define WC_CORE_ASSERT_H

#include <assert.h>

#if defined(NDEBUG)
#define WC_ASSERT(condition) ((condition) ? (void)0 : __builtin_trap())
#else
#define WC_ASSERT(condition) assert(condition)
#endif

#define WC_UNREACHABLE() __builtin_trap()

#endif
