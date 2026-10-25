#ifndef WRENCOTE_STATUS_H
#define WRENCOTE_STATUS_H

#include <stdint.h>

#include "wrencote/export.h"

typedef enum wc_status : int32_t {
    WC_OK = 0,
    WC_EINVAL = 1,
    WC_ENOMEM = 2,
    WC_EPERM = 3,
    WC_EACCES = 4,
    WC_ENOENT = 5,
    WC_EOVERFLOW = 6,
    WC_EPROTO = 7,
    WC_EIO = 8,
    WC_ECANCELED = 9,
    WC_EBUSY = 10,
    WC_ENOSPC = 11,
    WC_EAGAIN = 12
} wc_status;

/** Returns a stable human-readable name for a status. Never NULL. */
WC_API const char *wc_strerror(wc_status status);

#endif
