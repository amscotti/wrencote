if(WRENCOTE_SANITIZE AND WRENCOTE_TSAN)
    message(FATAL_ERROR "WRENCOTE_SANITIZE and WRENCOTE_TSAN are mutually exclusive")
endif()
if(WRENCOTE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 14 AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        # gcc-13's libasan on aarch64 hangs inside Wren string concatenation
        # (tests/data/fs_errors.wren "big + big" loop spins allocating). gcc-14
        # and clang are fine; see docs/toolchain.md.
        message(WARNING "gcc <= 13 + ASan on aarch64 is known to hang in Wren string \
concatenation; prefer clang or set -DWRENCOTE_SANITIZE=OFF")
    endif()
endif()
if(WRENCOTE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(WRENCOTE_COVERAGE)
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
endif()
