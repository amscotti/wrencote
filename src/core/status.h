#ifndef WC_CORE_STATUS_H
#define WC_CORE_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "core/compiler.h"

#include "wrencote/status.h"

#define WC_ERROR_MESSAGE_MAX 192

typedef struct wc_error {
    wc_status code;
    bool truncated;
    char msg[WC_ERROR_MESSAGE_MAX];
} wc_error;

/* Clears the error to WC_OK with an empty message. */
void wc_error_clear(wc_error *error);

/* Sets a status and copies [message], marking truncation instead of hiding it. */
void wc_error_set(wc_error *error, wc_status status, const char *message);

/* Formats a status message; truncation sets error->truncated. */
void wc_error_setf(wc_error *error, wc_status status, const char *fmt, ...) WC_PRINTF(3, 4);

#endif
