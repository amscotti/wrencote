#ifndef WRENCOTE_VERSION_H
#define WRENCOTE_VERSION_H

#include "wrencote/export.h"

#define WC_VERSION_MAJOR 0
#define WC_VERSION_MINOR 2
#define WC_VERSION_PATCH 1
#define WC_VERSION_STRING "0.2.1"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the library version as a NUL-terminated string.
 *
 * The returned pointer has static storage duration and must not be freed.
 */
WC_API const char *wc_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
