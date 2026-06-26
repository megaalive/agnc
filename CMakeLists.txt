cmake_minimum_required(VERSION 3.20)

project(agnc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_GENERATOR MATCHES "Ninja" AND MSVC)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_executable(agnc
    src/main.c
    src/cli/args.c
    src/cli/console.c
    src/cli/doctor.c
    src/cli/markdown_render.c
    src/cli/print.c
    src/config/config.c
    src/engine/query.c
    src/net/http.c
    src/net/sse.c
    src/permissions/permissions.c
    src/tools/read_file.c
    src/tools/shell.c
    src/util/atomic_write.c
    src/util/path.c
    src/util/status.c
)

target_include_directories(agnc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

agnc_set_compiler_warnings(agnc)
agnc_link_dependencies(agnc)

if(WIN32)
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_MACOS=1)
else()
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_LINUX=1)
endif()

install(TARGETS agnc RUNTIME DESTINATION bin)

option(AGNC_BUILD_TESTS "Build agnc unit tests" ON)

if(AGNC_BUILD_TESTS)
    find_package(cmocka CONFIG REQUIRED)
    enable_testing()

    add_executable(test_sse
        tests/test_sse.c
        src/net/sse.c
    )

    target_include_directories(test_sse PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_sse PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_sse)

    add_test(NAME test_sse COMMAND test_sse)

    add_executable(test_shell
        tests/test_shell.c
        src/tools/shell.c
    )

    target_include_directories(test_shell PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_shell PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_shell)

    add_test(NAME test_shell COMMAND test_shell)

    add_executable(test_markdown_render
        tests/test_markdown_render.c
        src/cli/console.c
        src/cli/markdown_render.c
    )

    target_include_directories(test_markdown_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    agnc_set_compiler_warnings(test_markdown_render)

    add_test(NAME test_markdown_render COMMAND test_markdown_render)

    add_executable(test_atomic_write
        tests/test_atomic_write.c
        src/config/config.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
    )

    target_include_directories(test_atomic_write PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_atomic_write PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_atomic_write)

    add_test(NAME test_atomic_write COMMAND test_atomic_write)
endif()
