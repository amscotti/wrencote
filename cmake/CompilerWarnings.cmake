function(wrencote_set_warnings target)
    set(common
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-qual -Wstrict-prototypes
        -Wmissing-prototypes -Wvla -Wformat=2 -Wundef -Wwrite-strings
        -Wdouble-promotion -Wnull-dereference)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        list(APPEND common -Wduplicated-branches -Wlogical-op -Wmaybe-uninitialized)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND common -Wconditional-uninitialized -Wcomma)
    endif()
    target_compile_options(${target} PRIVATE ${common})
    if(WRENCOTE_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
