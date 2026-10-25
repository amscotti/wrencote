#ifndef WC_CORE_LOG_H
#define WC_CORE_LOG_H

#include <stddef.h>

#include "core/compiler.h"

typedef enum wc_log_level {
    WC_LOG_ERROR = 0,
    WC_LOG_WARN = 1,
    WC_LOG_INFO = 2,
    WC_LOG_DEBUG = 3
} wc_log_level;

/* Receives one formatted line, without a trailing newline. */
typedef void (*wc_log_sink)(void *user, wc_log_level level, const char *line, size_t len);

typedef struct wc_logger {
    wc_log_sink sink;
    void *user;
    wc_log_level level;
} wc_logger;

void wc_logger_init(wc_logger *logger, wc_log_sink sink, void *user, wc_log_level level);

/* Formats into a bounded stack buffer; overlong messages are truncated and
 * marked with a trailing "...". */
void wc_logf(wc_logger *logger, wc_log_level level, const char *fmt, ...) WC_PRINTF(3, 4);

#endif
