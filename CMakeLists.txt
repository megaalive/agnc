cmake_minimum_required(VERSION 3.20)

# Auto-detect vcpkg toolchain sebelum project() — wajib untuk Open Folder VS (yyjson).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Vcpkg.cmake)

project(agnc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_GENERATOR MATCHES "Ninja" AND MSVC)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

set(AGNC_PROVIDER_SOURCES
    generated/agnc_integrations_gen.c
    src/integrations/registry.c
    src/providers/provider.c
)

add_executable(agnc
    src/main.c
    src/cli/args.c
    src/cli/console.c
    src/cli/doctor.c
    src/cli/markdown_render.c
    src/cli/print.c
    src/cli/repl.c
    src/cli/line_edit.c
    src/config/config.c
    src/engine/conversation.c
    src/engine/query.c
    src/engine/session.c
    src/engine/skills.c
    src/engine/hooks.c
    src/net/http.c
    src/net/sse.c
    src/permissions/permissions.c
    src/tools/read_file.c
    src/tools/shell.c
    src/tools/tool_path.c
    src/tools/write_file.c
    src/tools/edit_file.c
    src/tools/grep.c
    src/tools/rg_locate.c
    src/tools/ctags_locate.c
    src/tools/find_symbol.c
    src/tools/glob.c
    src/engine/tool_cache.c
    src/tools/web_fetch.c
    src/tools/todo_write.c
    src/mcp/client.c
    src/mcp/jsonrpc.c
    src/mcp/registry.c
    src/mcp/session.c
    src/mcp/stdio.c
    src/mcp/tools.c
    src/util/atomic_write.c
    src/util/path.c
    src/util/status.c
    ${AGNC_PROVIDER_SOURCES}
)

target_include_directories(agnc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

agnc_set_compiler_warnings(agnc)
agnc_link_dependencies(agnc)

if(WIN32)
    # AGNC_BUILDING: aktifkan AGNC_API dllexport di args.c (simbol CLI publik).
    target_compile_definitions(agnc PRIVATE AGNC_BUILDING AGNC_PLATFORM_WINDOWS=1)
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
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_link_libraries(test_atomic_write PRIVATE yyjson::yyjson cmocka::cmocka)
    target_sources(test_atomic_write PRIVATE
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
    )
    agnc_set_compiler_warnings(test_atomic_write)

    add_test(NAME test_atomic_write COMMAND test_atomic_write)

    add_executable(test_tool_path
        tests/test_tool_path.c
        src/tools/tool_path.c
        src/tools/write_file.c
        src/tools/edit_file.c
        src/tools/grep.c
        src/tools/rg_locate.c
        src/tools/ctags_locate.c
        src/tools/find_symbol.c
        src/util/path.c
        src/util/atomic_write.c
    )

    target_include_directories(test_tool_path PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_tool_path PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_tool_path)

    add_test(NAME test_tool_path COMMAND test_tool_path)

    add_executable(test_tool_cache
        tests/test_tool_cache.c
        src/engine/tool_cache.c
    )

    target_include_directories(test_tool_cache PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_tool_cache PRIVATE cmocka::cmocka)
    agnc_set_compiler_warnings(test_tool_cache)

    add_test(NAME test_tool_cache COMMAND test_tool_cache)

    add_executable(test_skills
        tests/test_skills.c
        src/engine/skills.c
        src/tools/tool_path.c
        src/util/path.c
    )

    target_include_directories(test_skills PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_skills PRIVATE cmocka::cmocka)
    agnc_set_compiler_warnings(test_skills)

    add_test(NAME test_skills COMMAND test_skills)

    add_executable(test_hooks
        tests/test_hooks.c
        src/engine/hooks.c
        src/util/path.c
    )

    target_include_directories(test_hooks PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_hooks PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_hooks)

    add_test(NAME test_hooks COMMAND test_hooks)

    add_executable(test_provider_registry
        tests/test_provider_registry.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
        src/providers/provider.c
        src/net/http.c
    )

    target_include_directories(test_provider_registry PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_link_libraries(test_provider_registry PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    agnc_set_compiler_warnings(test_provider_registry)

    add_test(NAME test_provider_registry COMMAND test_provider_registry)

    add_executable(test_config_provider
        tests/test_config_provider.c
        src/config/config.c
        src/util/path.c
        src/util/status.c
        src/util/atomic_write.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
        src/providers/provider.c
        src/net/http.c
    )

    target_include_directories(test_config_provider PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_compile_definitions(test_config_provider PRIVATE AGNC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_config_provider PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    agnc_set_compiler_warnings(test_config_provider)

    add_test(NAME test_config_provider COMMAND test_config_provider)

    add_executable(test_session
        tests/test_session.c
        src/engine/conversation.c
        src/engine/session.c
        src/config/config.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
    )

    target_include_directories(test_session PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_link_libraries(test_session PRIVATE yyjson::yyjson unofficial::sqlite3::sqlite3 cmocka::cmocka)
    agnc_set_compiler_warnings(test_session)

    add_test(NAME test_session COMMAND test_session)

    add_executable(test_mcp_jsonrpc
        tests/test_mcp_jsonrpc.c
        src/mcp/jsonrpc.c
        src/util/status.c
    )

    target_include_directories(test_mcp_jsonrpc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_mcp_jsonrpc PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_mcp_jsonrpc)

    add_test(NAME test_mcp_jsonrpc COMMAND test_mcp_jsonrpc)

    add_executable(mcp_mock_server
        tests/fixtures/mcp_mock_server.c
    )

    agnc_set_compiler_warnings(mcp_mock_server)

    add_executable(test_mcp_stdio
        tests/test_mcp_stdio.c
        src/mcp/client.c
        src/mcp/stdio.c
        src/mcp/jsonrpc.c
        src/util/status.c
    )

    target_include_directories(test_mcp_stdio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_definitions(test_mcp_stdio PRIVATE MCP_MOCK_SERVER="$<TARGET_FILE:mcp_mock_server>")
    target_link_libraries(test_mcp_stdio PRIVATE yyjson::yyjson cmocka::cmocka)
    add_dependencies(test_mcp_stdio mcp_mock_server)
    agnc_set_compiler_warnings(test_mcp_stdio)

    add_test(NAME test_mcp_stdio COMMAND test_mcp_stdio)

    add_executable(test_mcp_registry
        tests/test_mcp_registry.c
        src/config/config.c
        src/mcp/client.c
        src/mcp/registry.c
        src/mcp/stdio.c
        src/mcp/jsonrpc.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
        src/providers/provider.c
        src/net/http.c
    )

    target_include_directories(test_mcp_registry PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_compile_definitions(test_mcp_registry PRIVATE
        AGNC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        MCP_MOCK_SERVER="$<TARGET_FILE:mcp_mock_server>"
    )
    target_link_libraries(test_mcp_registry PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    add_dependencies(test_mcp_registry mcp_mock_server)
    agnc_set_compiler_warnings(test_mcp_registry)

    add_test(NAME test_mcp_registry COMMAND test_mcp_registry)

    add_executable(test_mcp_tools
        tests/test_mcp_tools.c
        src/mcp/client.c
        src/mcp/registry.c
        src/mcp/stdio.c
        src/mcp/jsonrpc.c
        src/mcp/tools.c
        src/config/config.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
        src/providers/provider.c
        src/net/http.c
    )

    target_include_directories(test_mcp_tools PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_compile_definitions(test_mcp_tools PRIVATE MCP_MOCK_SERVER="$<TARGET_FILE:mcp_mock_server>")
    target_link_libraries(test_mcp_tools PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    add_dependencies(test_mcp_tools mcp_mock_server)
    agnc_set_compiler_warnings(test_mcp_tools)

    add_test(NAME test_mcp_tools COMMAND test_mcp_tools)

    add_executable(test_mcp_filesystem_e2e
        tests/test_mcp_filesystem_e2e.c
        src/mcp/client.c
        src/mcp/registry.c
        src/mcp/stdio.c
        src/mcp/jsonrpc.c
        src/mcp/tools.c
        src/config/config.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
        generated/agnc_integrations_gen.c
        src/integrations/registry.c
        src/providers/provider.c
        src/net/http.c
    )

    target_include_directories(test_mcp_filesystem_e2e PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
    )

    target_compile_definitions(test_mcp_filesystem_e2e PRIVATE AGNC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_mcp_filesystem_e2e PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    agnc_set_compiler_warnings(test_mcp_filesystem_e2e)

    add_test(NAME test_mcp_filesystem_e2e COMMAND test_mcp_filesystem_e2e)
    set_tests_properties(test_mcp_filesystem_e2e PROPERTIES TIMEOUT 180)

    add_executable(test_todo_write
        tests/test_todo_write.c
        src/tools/todo_write.c
        src/util/atomic_write.c
        src/util/path.c
        src/util/status.c
    )

    target_include_directories(test_todo_write PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_todo_write PRIVATE yyjson::yyjson cmocka::cmocka)
    agnc_set_compiler_warnings(test_todo_write)

    add_test(NAME test_todo_write COMMAND test_todo_write)

    add_executable(test_web_fetch
        tests/test_web_fetch.c
        src/tools/web_fetch.c
        src/net/http.c
        src/util/status.c
    )

    target_include_directories(test_web_fetch PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_web_fetch PRIVATE yyjson::yyjson CURL::libcurl cmocka::cmocka)
    agnc_set_compiler_warnings(test_web_fetch)

    add_test(NAME test_web_fetch COMMAND test_web_fetch)
endif()
