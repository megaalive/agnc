/*
 * test_mcp_registry.c
 *
 * Test loader mcp.servers[] dan registry multi-server.
 */

#include "agnc/config.h"
#include "agnc/mcp/registry.h"

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

#ifndef MCP_MOCK_SERVER
#define MCP_MOCK_SERVER "mcp_mock_server"
#endif

#ifdef _WIN32
#include <stdlib.h>
#endif

static void test_config_load_mcp_servers(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char fixture_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
#endif

    snprintf(fixture_path, sizeof(fixture_path), "%s/tests/fixtures/mcp_config_test.json", AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load(fixture_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(config.mcp_server_count, 2);
    assert_string_equal(config.mcp_servers[0].id, "workspace-fs");
    assert_true(config.mcp_servers[0].enabled);
    assert_string_equal(config.mcp_servers[0].command, "npx");
    assert_int_equal(config.mcp_servers[0].arg_count, 3);
    assert_string_equal(config.mcp_servers[0].args[0], "-y");
    assert_string_equal(config.mcp_servers[0].args[1], "@modelcontextprotocol/server-filesystem");
    assert_false(config.mcp_servers[1].enabled);
    assert_string_equal(config.mcp_servers[1].id, "second-server");

    agnc_config_free(&config);
}

#ifdef _WIN32
static void test_registry_connect_mock_server(void **state)
{
    agnc_config_t config;
    agnc_mcp_registry_t registry;
    agnc_mcp_server_config_t server;
    agnc_status_t status;
    const agnc_mcp_connected_server_t *connected;

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
    assert_int_equal(agnc_mcp_registry_server_count(&registry), 1);

    connected = agnc_mcp_registry_server_at(&registry, 0);
    assert_non_null(connected);
    assert_string_equal(connected->server_id, "mock");
    assert_non_null(connected->tools_json);
    assert_non_null(strstr(connected->tools_json, "mock_tool"));

    agnc_mcp_registry_free(&registry);
    config.mcp_servers = NULL;
    config.mcp_server_count = 0;
    agnc_config_free(&config);
}
#endif

int main(void)
{
#ifdef _WIN32
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_load_mcp_servers),
        cmocka_unit_test(test_registry_connect_mock_server),
    };
#else
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_load_mcp_servers),
    };
#endif

    return cmocka_run_group_tests(tests, NULL, NULL);
}
