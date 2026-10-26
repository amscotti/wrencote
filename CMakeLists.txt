cmake_minimum_required(VERSION 4.0)
project(wrencote VERSION 0.3.0 LANGUAGES C)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
option(WRENCOTE_BUILD_TLS "Build the TLS transport (requires OpenSSL)" ON)
option(WRENCOTE_ENABLE_TLS "Compile TLS into libwrencote" ON)
option(WRENCOTE_ENABLE_HTTP2 "Compile the nghttp2 HTTP/2 adapter" ON)
if(WRENCOTE_BUILD_TLS OR WRENCOTE_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
endif()
if(WRENCOTE_ENABLE_HTTP2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)
endif()

include(GNUInstallDirs)
include(CheckCCompilerFlag)
include(cmake/CStandard.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Hardening.cmake)

option(WRENCOTE_BUILD_TESTS "Build tests" ON)
option(WRENCOTE_WERROR "Treat warnings as errors" OFF)
option(WRENCOTE_SANITIZE "Address+Undefined sanitizers" OFF)
option(WRENCOTE_TSAN "Thread sanitizer" OFF)
option(WRENCOTE_COVERAGE "Coverage instrumentation" OFF)
option(WRENCOTE_HARDEN "Release hardening" OFF)
option(WRENCOTE_REPRODUCIBLE "Map build paths and drop build IDs" ON)

if(WRENCOTE_REPRODUCIBLE)
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.")
    add_link_options("-Wl,--build-id=none")
endif()

set(WRENCOTE_CORE_SOURCES
    src/core/version.c
    src/core/mem.c
    src/core/status.c
    src/core/arena.c
    src/core/buf.c
    src/core/str.c
    src/core/log.c
    src/core/clock.c
    src/core/hash.c)

set(WRENCOTE_LOOP_SOURCES
    src/loop/poll.c
    src/loop/timer.c
    src/loop/loop.c
    src/loop/transport.c)

set(WRENCOTE_HTTP_SOURCES
    src/http/parser.c
    src/http/response.c
    src/http/server.c)

set(WRENCOTE_HTTP2_SOURCES
    src/http2/frame.c)
if(WRENCOTE_ENABLE_HTTP2)
    list(APPEND WRENCOTE_HTTP2_SOURCES src/http2/server.c)
endif()

set(WRENCOTE_TLS_OBJECT_SOURCES)
if(WRENCOTE_ENABLE_TLS)
    list(APPEND WRENCOTE_TLS_OBJECT_SOURCES src/tls/tls.c src/tls/transport.c)
endif()

set(STDLIB_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/stdlib.c)
set(STDLIB_INPUTS
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/json.wren
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/router.wren)
add_custom_command(
    OUTPUT ${STDLIB_GENERATED}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_stdlib.py
            ${CMAKE_CURRENT_SOURCE_DIR}/modules ${STDLIB_GENERATED}
    DEPENDS ${STDLIB_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_stdlib.py
    COMMENT "Generating embedded Wren stdlib")
set_source_files_properties(${STDLIB_GENERATED} PROPERTIES GENERATED TRUE)

set(WRENCOTE_VM_SOURCES
    ${STDLIB_GENERATED}
    src/vm/vm.c
    src/vm/memory.c
    src/vm/module.c
    src/vm/bindings.c)

set(WRENCOTE_MODULE_SOURCES
    src/modules/fs.c
    src/modules/sys.c)
find_package(CURL QUIET)
if(CURL_FOUND)
    list(APPEND WRENCOTE_MODULE_SOURCES src/modules/http.c)
endif()

set(WRENCOTE_SERVER_SOURCES
    src/server/host.c
    src/server/modules.c
    src/server/net.c
    src/server/sandbox.c)

set(WRENCOTE_WREN_SOURCES
    third_party/wren/src/vm/wren_compiler.c
    third_party/wren/src/vm/wren_core.c
    third_party/wren/src/vm/wren_debug.c
    third_party/wren/src/vm/wren_primitive.c
    third_party/wren/src/vm/wren_utils.c
    third_party/wren/src/vm/wren_value.c
    third_party/wren/src/vm/wren_vm.c
    third_party/wren/src/optional/wren_opt_meta.c
    third_party/wren/src/optional/wren_opt_random.c)

add_library(wrencote_wren_objects OBJECT ${WRENCOTE_WREN_SOURCES})
target_include_directories(wrencote_wren_objects PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/vm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/optional)
target_compile_options(wrencote_wren_objects PRIVATE -w)
set_target_properties(wrencote_wren_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(wrencote_objects OBJECT ${WRENCOTE_CORE_SOURCES} ${WRENCOTE_LOOP_SOURCES} ${WRENCOTE_HTTP_SOURCES} ${WRENCOTE_HTTP2_SOURCES} ${WRENCOTE_TLS_OBJECT_SOURCES} ${WRENCOTE_VM_SOURCES} ${WRENCOTE_MODULE_SOURCES} ${WRENCOTE_SERVER_SOURCES})
set_target_properties(wrencote_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
if(WRENCOTE_ENABLE_HTTP2)
    target_include_directories(wrencote_objects PRIVATE ${NGHTTP2_INCLUDE_DIRS})
    target_compile_definitions(wrencote_objects PRIVATE WC_ENABLE_HTTP2=1)
endif()
target_include_directories(wrencote_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(wrencote_objects PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/vm)
target_compile_definitions(wrencote_objects PRIVATE WC_BUILDING_LIBRARY)
if(WRENCOTE_ENABLE_TLS)
    target_compile_definitions(wrencote_objects PRIVATE WC_ENABLE_TLS=1)
    # The TLS sources need the OpenSSL headers; PRIVATE usage requirements are
    # applied when compiling the object library itself.
    target_link_libraries(wrencote_objects PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_include_directories(wrencote_objects PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()
if(CURL_FOUND)
    target_include_directories(wrencote_objects PRIVATE ${CURL_INCLUDE_DIRS})
    target_compile_definitions(wrencote_objects PRIVATE WC_ENABLE_HTTP_CLIENT=1)
endif()
wrencote_set_warnings(wrencote_objects)

add_library(wrencote_static STATIC
    $<TARGET_OBJECTS:wrencote_objects>
    $<TARGET_OBJECTS:wrencote_wren_objects>)
add_library(wrencote::static ALIAS wrencote_static)
set_target_properties(wrencote_static PROPERTIES OUTPUT_NAME wrencote)
target_link_libraries(wrencote_static INTERFACE m)
if(WRENCOTE_ENABLE_TLS)
    target_link_libraries(wrencote_static INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(WRENCOTE_ENABLE_HTTP2)
    target_link_libraries(wrencote_static INTERFACE PkgConfig::NGHTTP2)
endif()
if(CURL_FOUND)
    target_link_libraries(wrencote_static INTERFACE CURL::libcurl)
endif()

add_library(wrencote_shared SHARED
    $<TARGET_OBJECTS:wrencote_objects>
    $<TARGET_OBJECTS:wrencote_wren_objects>)
add_library(wrencote::shared ALIAS wrencote_shared)
set_target_properties(wrencote_shared PROPERTIES
    OUTPUT_NAME wrencote
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/wrencote.map)
target_link_options(wrencote_shared PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/wrencote.map")
if(NOT WRENCOTE_SANITIZE AND NOT WRENCOTE_TSAN)
    # An instrumented DSO intentionally leaves the sanitizer runtime hooks
    # (__asan_report_*, __ubsan_handle_*) undefined for the host executable to
    # provide; --no-undefined would reject those links. Release builds keep the
    # strict check.
    target_link_options(wrencote_shared PRIVATE "-Wl,--no-undefined")
endif()
target_link_libraries(wrencote_shared PRIVATE m)
if(WRENCOTE_ENABLE_TLS)
    target_link_libraries(wrencote_shared PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(WRENCOTE_ENABLE_HTTP2)
    target_link_libraries(wrencote_shared PRIVATE PkgConfig::NGHTTP2)
endif()
if(CURL_FOUND)
    target_link_libraries(wrencote_shared PRIVATE CURL::libcurl)
endif()

foreach(target wrencote_static wrencote_shared)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endforeach()

if(OpenSSL_FOUND)
    add_library(wrencote_tls STATIC src/tls/tls.c src/tls/transport.c src/loop/transport.c src/core/mem.c src/core/status.c)
    target_include_directories(wrencote_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(wrencote_tls PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    wrencote_set_warnings(wrencote_tls)
    target_compile_definitions(wrencote_tls PRIVATE WC_ENABLE_TLS=1)
endif()

add_executable(wrencote_cli src/cli/main.c)
set_target_properties(wrencote_cli PROPERTIES OUTPUT_NAME wrencote)
target_link_libraries(wrencote_cli PRIVATE wrencote_static)
target_include_directories(wrencote_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
wrencote_set_warnings(wrencote_cli)

if(WRENCOTE_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    function(wc_add_test name)
        add_executable(${name} tests/unit/${name}.c)
        target_link_libraries(${name} PRIVATE wrencote_static Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include)
        target_compile_options(${name} PRIVATE -UNDEBUG)
        wrencote_set_warnings(${name})
        add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
        set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    endfunction()
    wc_add_test(test_version)
    wc_add_test(test_arena)
    wc_add_test(test_buf)
    wc_add_test(test_str)
    wc_add_test(test_status)
    wc_add_test(test_log)
    wc_add_test(test_clock)
    wc_add_test(test_hash)
    wc_add_test(test_alloc_budget)
    wc_add_test(test_mem)
    wc_add_test(test_transport)
    wc_add_test(test_timers)
    wc_add_test(test_loop_echo)
    wc_add_test(test_wakeup)
    wc_add_test(test_fd_stability)
    wc_add_test(test_signal)
    wc_add_test(test_poll)
    wc_add_test(test_timer_heap)
    wc_add_test(test_http_parser)
    wc_add_test(test_http_response)
    wc_add_test(test_http_config)
    wc_add_test(test_h2_frame)
    if(WRENCOTE_ENABLE_HTTP2)
        wc_add_test(test_h2_server)
        target_link_libraries(test_h2_server PRIVATE PkgConfig::NGHTTP2)
    endif()
    if(OpenSSL_FOUND)
        add_executable(test_tls tests/unit/test_tls.c)
        target_link_libraries(test_tls PRIVATE wrencote_tls OpenSSL::SSL OpenSSL::Crypto)
        target_include_directories(test_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        wrencote_set_warnings(test_tls)
        target_compile_options(test_tls PRIVATE -UNDEBUG)
        add_test(NAME test_tls COMMAND $<TARGET_FILE:test_tls>)
        add_executable(test_tls_transport tests/unit/test_tls_transport.c)
        target_link_libraries(test_tls_transport PRIVATE wrencote_tls OpenSSL::SSL OpenSSL::Crypto)
        target_include_directories(test_tls_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        wrencote_set_warnings(test_tls_transport)
        target_compile_options(test_tls_transport PRIVATE -UNDEBUG)
        add_test(NAME test_tls_transport COMMAND $<TARGET_FILE:test_tls_transport>)
        add_executable(test_tls_alpn tests/unit/test_tls_alpn.c)
        target_link_libraries(test_tls_alpn PRIVATE wrencote_tls OpenSSL::SSL OpenSSL::Crypto)
        target_include_directories(test_tls_alpn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        wrencote_set_warnings(test_tls_alpn)
        target_compile_options(test_tls_alpn PRIVATE -UNDEBUG)
        add_test(NAME test_tls_alpn COMMAND $<TARGET_FILE:test_tls_alpn>)
    endif()
    add_executable(test_sandbox_syscalls tests/integration/test_sandbox_syscalls.c)
    target_link_libraries(test_sandbox_syscalls PRIVATE wrencote_static)
    target_include_directories(test_sandbox_syscalls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    wrencote_set_warnings(test_sandbox_syscalls)
    target_compile_options(test_sandbox_syscalls PRIVATE -UNDEBUG)
    add_test(NAME test_sandbox_syscalls COMMAND $<TARGET_FILE:test_sandbox_syscalls>)
    set_tests_properties(test_sandbox_syscalls PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    add_test(NAME test_sandbox
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_sandbox.sh ${CMAKE_BINARY_DIR})
    set_tests_properties(test_sandbox PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    if(WRENCOTE_ENABLE_TLS AND WRENCOTE_ENABLE_HTTP2)
        find_package(Python3 COMPONENTS Interpreter)
        if(NOT WRENCOTE_TSAN)
            add_test(NAME test_h2_unique
                COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_h2_unique.sh ${CMAKE_BINARY_DIR})
            set_tests_properties(test_h2_unique PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
            add_test(NAME test_h2_idle
                COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_h2_idle.sh ${CMAKE_BINARY_DIR})
            set_tests_properties(test_h2_idle PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
        endif()
        add_test(NAME test_h2_slow_reader
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_h2_slow_reader.sh ${CMAKE_BINARY_DIR})
        set_tests_properties(test_h2_slow_reader PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
        add_test(NAME test_http_client
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_http_client.sh ${CMAKE_BINARY_DIR})
        set_tests_properties(test_http_client PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
        add_test(NAME test_sse
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_sse.sh ${CMAKE_BINARY_DIR})
        set_tests_properties(test_sse PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
        add_test(NAME test_http2_server
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_http2_server.sh ${CMAKE_BINARY_DIR})
        set_tests_properties(test_http2_server PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    endif()
    if(WRENCOTE_ENABLE_TLS)
        add_test(NAME test_tls_server
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_tls_server.sh ${CMAKE_BINARY_DIR})
        set_tests_properties(test_tls_server PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    endif()
    add_test(NAME test_cli
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_cli.sh ${CMAKE_BINARY_DIR})
    set_tests_properties(test_cli PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    add_executable(test_stdlib tests/integration/test_stdlib.c)
    target_link_libraries(test_stdlib PRIVATE wrencote_static)
    target_include_directories(test_stdlib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include)
    wrencote_set_warnings(test_stdlib)
    target_compile_options(test_stdlib PRIVATE -UNDEBUG)
    add_test(NAME test_stdlib COMMAND $<TARGET_FILE:test_stdlib>)
    set_tests_properties(test_stdlib PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    add_executable(test_http_server tests/integration/test_http_server.c)
    target_link_libraries(test_http_server PRIVATE wrencote_static Threads::Threads)
    target_include_directories(test_http_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include)
    wrencote_set_warnings(test_http_server)
    target_compile_options(test_http_server PRIVATE -UNDEBUG)
    add_test(NAME test_http_server COMMAND $<TARGET_FILE:test_http_server>)
    set_tests_properties(test_http_server PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
    wc_add_test(test_vm)
    wc_add_test(test_host)
endif()

file(GLOB_RECURSE WC_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${WC_FORMAT_SOURCES}
        COMMENT "Checking clang-format")
else()
    message(STATUS "clang-format not found; format-check target unavailable")
endif()

file(GLOB_RECURSE WC_TIDY_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c)
find_program(CLANG_TIDY_EXECUTABLE clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR} --extra-arg=-Wno-unknown-warning-option ${WC_TIDY_SOURCES}
        COMMENT "Running clang-tidy")
else()
    message(STATUS "clang-tidy not found; tidy target unavailable")
endif()

find_program(GCOVR_EXECUTABLE gcovr)
add_custom_target(coverage
    # Landlock blocks gcov's output path in the sandbox tests, so sandbox.c is
    # covered behaviourally by test_sandbox.sh/test_sandbox_syscalls and excluded
    # from the line-coverage denominator.
    COMMAND ${GCOVR_EXECUTABLE} --root ${CMAKE_CURRENT_SOURCE_DIR}
            --filter ${CMAKE_CURRENT_SOURCE_DIR}/src
            --exclude ${CMAKE_CURRENT_SOURCE_DIR}/src/server/sandbox.c
            --exclude ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/http.c
            --fail-under-line 85
            --print-summary
    COMMENT "Coverage for src/")

option(WRENCOTE_BUILD_FUZZERS "Build libFuzzer targets" OFF)
if(WRENCOTE_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "WRENCOTE_BUILD_FUZZERS requires Clang (libFuzzer)")
    endif()
    add_executable(fuzz_http_request
        tests/fuzz/fuzz_http_request.c
        src/http/parser.c
        src/core/str.c)
    target_include_directories(fuzz_http_request PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wren/src/include)
    target_compile_options(fuzz_http_request PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(fuzz_http_request PRIVATE -fsanitize=fuzzer,address,undefined)
    if(WRENCOTE_ENABLE_HTTP2)
        # Compiles the adapter sources directly (like its sibling targets) so
        # libFuzzer drives the nghttp2 adapter's coverage, not just the
        # harness's.
        add_executable(fuzz_h2_feed tests/fuzz/fuzz_h2_feed.c src/http2/server.c
                                        src/http2/frame.c src/core/mem.c src/core/buf.c
                                        src/core/status.c src/core/str.c)
        target_include_directories(fuzz_h2_feed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include ${NGHTTP2_INCLUDE_DIRS})
        target_compile_options(fuzz_h2_feed PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
        target_link_options(fuzz_h2_feed PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_libraries(fuzz_h2_feed PRIVATE ${NGHTTP2_LIBRARIES} m)
        add_executable(fuzz_h2_frame tests/fuzz/fuzz_h2_frame.c src/http2/frame.c)
        target_include_directories(fuzz_h2_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_options(fuzz_h2_frame PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
        target_link_options(fuzz_h2_frame PRIVATE -fsanitize=fuzzer,address,undefined)
    endif()
endif()

include(CMakePackageConfigHelpers)
install(TARGETS wrencote_static wrencote_shared wrencote_cli
    EXPORT wrencoteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT wrencoteTargets
    FILE wrencoteTargets.cmake
    NAMESPACE Wrencote::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wrencote)
configure_package_config_file(cmake/wrencoteConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/wrencoteConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wrencote)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/wrencoteConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/wrencoteConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/wrencoteConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wrencote)
configure_file(cmake/wrencote.pc.in ${CMAKE_CURRENT_BINARY_DIR}/wrencote.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wrencote.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
