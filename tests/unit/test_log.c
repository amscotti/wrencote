#include <assert.h>
#include <string.h>

#include "core/log.h"

typedef struct capture {
    int calls;
    wc_log_level level;
    char line[1024];
} capture;

static void sink(void *user, wc_log_level level, const char *line, size_t len) {
    capture *out = user;
    out->calls++;
    out->level = level;
    size_t copy = len < sizeof out->line - 1 ? len : sizeof out->line - 1;
    memcpy(out->line, line, copy);
    out->line[copy] = '\0';
}

int main(void) {
    capture out = {0};
    wc_logger logger;
    wc_logger_init(&logger, sink, &out, WC_LOG_INFO);

    wc_logf(&logger, WC_LOG_ERROR, "failed %s=%d", "code", 7);
    assert(out.calls == 1);
    assert(out.level == WC_LOG_ERROR);
    assert(strcmp(out.line, "failed code=7") == 0);

    wc_logf(&logger, WC_LOG_DEBUG, "hidden");
    assert(out.calls == 1);

    char big[900];
    for (size_t i = 0; i < sizeof big - 1; i++) {
        big[i] = 'y';
    }
    big[sizeof big - 1] = '\0';
    wc_logf(&logger, WC_LOG_WARN, "%s", big);
    assert(out.calls == 2);
    assert(strlen(out.line) == 511);
    assert(strcmp(out.line + 508, "...") == 0);

    capture silent_out = {0};
    wc_logger silent;
    wc_logger_init(&silent, NULL, &silent_out, WC_LOG_DEBUG);
    wc_logf(&silent, WC_LOG_INFO, "ignored");
    assert(silent_out.calls == 0);
    wc_logf(NULL, WC_LOG_INFO, "ignored");
    wc_logf(&logger, WC_LOG_INFO, NULL);
    wc_logger_init(NULL, sink, &out, WC_LOG_INFO);
    return 0;
}
