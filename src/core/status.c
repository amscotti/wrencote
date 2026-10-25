#include "core/status.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *wc_strerror(wc_status status) {
    switch (status) {
    case WC_OK:
        return "ok";
    case WC_EINVAL:
        return "invalid argument";
    case WC_ENOMEM:
        return "out of memory";
    case WC_EPERM:
        return "operation not permitted";
    case WC_EACCES:
        return "permission denied";
    case WC_ENOENT:
        return "not found";
    case WC_EOVERFLOW:
        return "overflow";
    case WC_EPROTO:
        return "protocol error";
    case WC_EIO:
        return "input/output error";
    case WC_ECANCELED:
        return "canceled";
    case WC_EBUSY:
        return "busy";
    case WC_EAGAIN:
        return "EAGAIN";
    case WC_ENOSPC:
        return "no space left";
    }
    return "unknown status";
}

void wc_error_clear(wc_error *error) {
    if (error == NULL) {
        return;
    }
    error->code = WC_OK;
    error->truncated = false;
    error->msg[0] = '\0';
}

void wc_error_set(wc_error *error, wc_status status, const char *message) {
    if (error == NULL) {
        return;
    }
    error->code = status;
    error->truncated = false;
    if (message == NULL) {
        error->msg[0] = '\0';
        return;
    }
    int written = snprintf(error->msg, sizeof error->msg, "%s", message);
    if (written < 0) {
        error->msg[0] = '\0';
        error->truncated = true;
    } else if ((size_t)written >= sizeof error->msg) {
        error->truncated = true;
    }
}

void wc_error_setf(wc_error *error, wc_status status, const char *fmt, ...) {
    if (error == NULL) {
        return;
    }
    error->code = status;
    error->truncated = false;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(error->msg, sizeof error->msg, fmt, args);
    va_end(args);
    if (written < 0) {
        error->msg[0] = '\0';
        error->truncated = true;
    } else if ((size_t)written >= sizeof error->msg) {
        error->truncated = true;
    }
}
