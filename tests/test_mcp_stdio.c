/*
 * test_mcp_stdio.c
 *
 * Integration test transport MCP stdio + handshake client (mock server).
 */

#include "agnc/mcp/client.h"
#include "agnc/mcp/jsonrpc.h"
#include "agnc/mcp/stdio.h"

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
static void test_mcp_stdio_write_read_line(void **state)
{
    agnc_mcp_stdio_conn_t *conn = NULL;
    agnc_jsonrpc_message_t message;
    char *request;
    agnc_status_t status;

    (void)state;

    status = agnc_mcp_stdio_spawn(MCP_MOCK_SERVER, NULL, 0, NULL, NULL, NULL, 0, &conn);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(conn);

    request = agnc_jsonrpc_format_request(42, "tools/list", "{}");
    assert_non_null(request);

    assert_int_equal(agnc_mcp_stdio_write_line(conn, request), AGNC_STATUS_OK);
    free(request);

    agnc_jsonrpc_message_init(&message);
    status = agnc_mcp_stdio_read_message(conn, &message, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(message.is_response);
    assert_int_equal(message.id, 42);
    assert_false(message.has_error);
    assert_non_null(message.result_json);
    assert_non_null(strstr(message.result_json, "mock_tool"));

    agnc_jsonrpc_message_free(&message);
    agnc_mcp_stdio_close(conn);
}

static void test_mcp_client_handshake(void **state)
{
    agnc_mcp_client_t client;
    char *tools_json = NULL;
    agnc_status_t status;

    (void)state;

    agnc_mcp_client_init(&client);
    status = agnc_mcp_client_connect(
        MCP_MOCK_SERVER, NULL, 0, NULL, NULL, NULL, 0, &client, &tools_json, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(client.initialized);
    assert_non_null(tools_json);
    assert_non_null(strstr(tools_json, "mock_tool"));

    free(tools_json);
    agnc_mcp_client_close(&client);
}

static void test_mcp_client_call_tool(void **state)
{
    agnc_mcp_client_t client;
    char *result = NULL;
    agnc_status_t status;

    (void)state;

    agnc_mcp_client_init(&client);
    status = agnc_mcp_client_connect(
        MCP_MOCK_SERVER, NULL, 0, NULL, NULL, NULL, 0, &client, NULL, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);

    status = agnc_mcp_client_call_tool(&client, "mock_tool", "{}", &result, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(result);
    assert_string_equal(result, "mock ok");

    free(result);
    agnc_mcp_client_close(&client);
}

static void test_mcp_stdio_spawn_custom_env(void **state)
{
    agnc_mcp_stdio_conn_t *conn = NULL;
    agnc_jsonrpc_message_t response;
    const char *env_keys[] = {"AGNC_MCP_TEST_ENV"};
    const char *env_values[] = {"from-config"};
    char *request;
    agnc_status_t status;

    (void)state;

    status = agnc_mcp_stdio_spawn(
        MCP_MOCK_SERVER, NULL, 0, NULL, env_keys, env_values, 1, &conn);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(conn);

    request = agnc_jsonrpc_format_request(
        1,
        "initialize",
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"agnc\",\"version\":\"0.1.0\"}}");
    assert_non_null(request);

    assert_int_equal(agnc_mcp_stdio_write_line(conn, request), AGNC_STATUS_OK);
    free(request);

    agnc_jsonrpc_message_init(&response);
    status = agnc_mcp_stdio_read_message(conn, &response, 10000);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_false(response.has_error);
    assert_non_null(response.result_json);
    assert_non_null(strstr(response.result_json, "env:from-config"));

    agnc_jsonrpc_message_free(&response);
    agnc_mcp_stdio_close(conn);
}
#else
static void test_mcp_stdio_skipped_on_non_windows(void **state)
{
    (void)state;
    skip();
}
#endif

int main(void)
{
#ifdef _WIN32
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mcp_stdio_write_read_line),
        cmocka_unit_test(test_mcp_client_handshake),
        cmocka_unit_test(test_mcp_client_call_tool),
        cmocka_unit_test(test_mcp_stdio_spawn_custom_env),
    };
#else
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mcp_stdio_skipped_on_non_windows),
    };
#endif

    return cmocka_run_group_tests(tests, NULL, NULL);
}
