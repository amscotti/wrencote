#include <assert.h>
#include <string.h>

#include "core/status.h"

int main(void) {
    for (int status = WC_OK; status <= WC_ENOSPC; status++) {
        const char *text = wc_strerror((wc_status)status);
        assert(text != NULL);
        assert(text[0] != '\0');
    }
    assert(strcmp(wc_strerror(WC_OK), "ok") == 0);
    assert(strcmp(wc_strerror((wc_status)9999), "unknown status") == 0);

    wc_error error;
    wc_error_clear(&error);
    assert(error.code == WC_OK);
    assert(error.msg[0] == '\0');
    assert(!error.truncated);

    wc_error_set(&error, WC_ENOENT, "missing file");
    assert(error.code == WC_ENOENT);
    assert(strcmp(error.msg, "missing file") == 0);
    assert(!error.truncated);

    wc_error_setf(&error, WC_EINVAL, "bad %s %d", "value", 7);
    assert(error.code == WC_EINVAL);
    assert(strcmp(error.msg, "bad value 7") == 0);

    char big[WC_ERROR_MESSAGE_MAX + 32];
    memset(big, 'x', sizeof big);
    big[sizeof big - 1] = '\0';
    wc_error_set(&error, WC_EIO, big);
    assert(error.truncated);
    assert(strlen(error.msg) == WC_ERROR_MESSAGE_MAX - 1);
    wc_error_set(&error, WC_EIO, NULL);
    assert(error.msg[0] == '\0');
    assert(!error.truncated);
    wc_error_set(NULL, WC_EIO, "ignored");
    wc_error_setf(NULL, WC_EIO, "ignored");
    return 0;
}
