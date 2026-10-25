#ifndef WC_CORE_COMPILER_H
#define WC_CORE_COMPILER_H

#define WC_NODISCARD [[nodiscard]]
#define WC_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#define WC_UNUSED(value) ((void)(value))

#endif
