#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WC_LOG_LINE_MAX 512

void wc_logger_init(wc_logger *logger, wc_log_sink sink, void *user, wc_log_level level) {
    if (logger == NULL) {
        return;
    }
    logger->sink = sink;
    logger->user = user;
    logger->level = level;
}

void wc_logf(wc_logger *logger, wc_log_level level, const char *fmt, ...) {
    if (logger == NULL || logger->sink == NULL || fmt == NULL) {
        return;
    }
    if ((int)level > (int)logger->level) {
        return;
    }
    char line[WC_LOG_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    size_t len = 0;
    if (written < 0) {
        line[0] = '\0';
    } else if ((size_t)written >= sizeof line) {
        len = sizeof line - 1;
        line[len - 3] = '.';
        line[len - 2] = '.';
        line[len - 1] = '.';
        line[len] = '\0';
    } else {
        len = (size_t)written;
    }
    if (len == 0) {
        len = strlen(line);
    }
    logger->sink(logger->user, level, line, len);
}
