/*
 * test_mcp_tools.c
 *
 * Unit test katalog tool MCP (nama diekspos + schema).
 */

#include "agnc/mcp/registry.h"
#include "agnc/mcp/tools.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifndef MCP_MOCK_SERVER
#define MCP_MOCK_SERVER "mcp_mock_server"
#endif

#ifdef _WIN32
static void test_mcp_tool_catalog_exposed_name(void **state)
{
    agnc_config_t config;
    agnc_mcp_server_config_t server;
    agnc_mcp_registry_t registry;
    agnc_mcp_tool_catalog_t catalog;
    const agnc_mcp_runtime_tool_t *tool;
    agnc_status_t status;

    (void)state;

    memset(&server, 0, sizeof(server));
    server.id = "mock";
    server.command = MCP_MOCK_SERVER;
    server.enabled = 1;

    agnc_config_init(&config);
    config.mcp_servers = &server;
    config.mcp_server_count = 1;

    agnc_mcp_registry_init(&registry);
    status = agnc_mcp_registry_load_from_config(&config, &registry, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);

    agnc_mcp_tool_catalog_init(&catalog);
    status = agnc_mcp_tool_catalog_build(&registry, &catalog);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(catalog.count, 1);

    tool = agnc_mcp_tool_catalog_find(&catalog, "mcp_mock_mock_tool");
    assert_non_null(tool);
    assert_string_equal(tool->mcp_tool_name, "mock_tool");
    assert_non_null(tool->parameters_json);
    assert_non_null(strstr(tool->parameters_json, "\"type\":\"object\""));

    agnc_mcp_tool_catalog_free(&catalog);
    agnc_mcp_registry_free(&registry);
    config.mcp_servers = NULL;
    config.mcp_server_count = 0;
    agnc_config_free(&config);
}
#endif

static void test_mcp_tool_catalog_empty_registry(void **state)
{
    agnc_mcp_registry_t registry;
    agnc_mcp_tool_catalog_t catalog;
    agnc_status_t status;

    (void)state;

    agnc_mcp_registry_init(&registry);
    agnc_mcp_tool_catalog_init(&catalog);
    status = agnc_mcp_tool_catalog_build(&registry, &catalog);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(catalog.count, 0);

    agnc_mcp_tool_catalog_free(&catalog);
}

int main(void)
{
#ifdef _WIN32
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mcp_tool_catalog_empty_registry),
        cmocka_unit_test(test_mcp_tool_catalog_exposed_name),
    };
#else
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mcp_tool_catalog_empty_registry),
    };
#endif

    return cmocka_run_group_tests(tests, NULL, NULL);
}
