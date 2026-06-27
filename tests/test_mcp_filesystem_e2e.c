/*
 * test_mcp_filesystem_e2e.c
 *
 * E2E: server filesystem MCP nyata via npx (initialize, tools/list, tools/call).
 * Butuh Node/npx dan jaringan untuk unduh paket pertama kali.
 */

#include "agnc/config.h"
#include "agnc/mcp/client.h"
#include "agnc/mcp/registry.h"
#include "agnc/mcp/tools.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifndef AGNC_SOURCE_DIR
#define AGNC_SOURCE_DIR "."
#endif

#ifdef _WIN32
#include <stdlib.h>
#endif

#define AGNC_MCP_E2E_TIMEOUT_MS 120000

#ifdef _WIN32
static void test_mcp_filesystem_e2e_read_readme(void **state)
{
    agnc_config_t config;
    agnc_mcp_registry_t registry;
    agnc_mcp_tool_catalog_t catalog;
    const agnc_mcp_runtime_tool_t *read_tool;
    const agnc_mcp_connected_server_t *server;
    char fixture_path[512];
    char *result = NULL;
    agnc_status_t status;

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
#endif

    snprintf(fixture_path, sizeof(fixture_path), "%s/tests/fixtures/mcp_filesystem_e2e.json", AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load(fixture_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(config.mcp_server_count, 1);

    agnc_mcp_registry_init(&registry);
    status = agnc_mcp_registry_load_from_config(&config, &registry, AGNC_MCP_E2E_TIMEOUT_MS);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(agnc_mcp_registry_server_count(&registry), 1);

    agnc_mcp_tool_catalog_init(&catalog);
    status = agnc_mcp_tool_catalog_build(&registry, &catalog);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(catalog.count > 0);

    read_tool = agnc_mcp_tool_catalog_find(&catalog, "mcp_workspace-fs_read_file");
    assert_non_null(read_tool);

    server = agnc_mcp_registry_server_at(&registry, read_tool->server_index);
    assert_non_null(server);

    status = agnc_mcp_client_call_tool(
        (agnc_mcp_client_t *)&server->client,
        "read_file",
        "{\"path\":\"README.md\"}",
        &result,
        AGNC_MCP_E2E_TIMEOUT_MS);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(result);
    assert_true(strlen(result) > 0);
    assert_non_null(strstr(result, "agnc"));

    free(result);
    agnc_mcp_tool_catalog_free(&catalog);
    agnc_mcp_registry_free(&registry);
    agnc_config_free(&config);
}
#endif

int main(void)
{
#ifdef _WIN32
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mcp_filesystem_e2e_read_readme),
    };
#else
    const struct CMUnitTest tests[] = {};
#endif

    return cmocka_run_group_tests(tests, NULL, NULL);
}
