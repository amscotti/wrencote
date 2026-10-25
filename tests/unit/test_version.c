#include <assert.h>
#include <string.h>

#include "wrencote/version.h"

_Static_assert(WC_VERSION_MAJOR == 0, "major version");
_Static_assert(WC_VERSION_MINOR == 2, "minor version");
_Static_assert(WC_VERSION_PATCH == 1, "patch version");

int main(void) {
    assert(wc_version_string() != NULL);
    assert(strcmp(wc_version_string(), WC_VERSION_STRING) == 0);
    assert(strcmp(wc_version_string(), "0.2.1") == 0);
    return 0;
}
