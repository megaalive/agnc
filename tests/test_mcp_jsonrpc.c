/*
 * test_mcp_jsonrpc.c
 *
 * Unit test JSON-RPC 2.0 untuk lapisan MCP.
 */

#include "agnc/mcp/jsonrpc.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_jsonrpc_parse_request(void **state)
{
    agnc_jsonrpc_message_t message;
    agnc_status_t status;

    (void)state;

    agnc_jsonrpc_message_init(&message);
    status = agnc_jsonrpc_parse_line(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"a\":1}}",
        &message);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(message.is_request);
    assert_false(message.is_notification);
    assert_false(message.is_response);
    assert_true(message.has_id);
    assert_int_equal(message.id, 1);
    assert_string_equal(message.method, "initialize");
    assert_non_null(message.params_json);
    assert_string_equal(message.params_json, "{\"a\":1}");
    agnc_jsonrpc_message_free(&message);
}

static void test_jsonrpc_parse_notification(void **state)
{
    agnc_jsonrpc_message_t message;

    (void)state;

    agnc_jsonrpc_message_init(&message);
    assert_int_equal(
        agnc_jsonrpc_parse_line(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
            &message),
        AGNC_STATUS_OK);
    assert_true(message.is_notification);
    assert_string_equal(message.method, "notifications/initialized");
    agnc_jsonrpc_message_free(&message);
}

static void test_jsonrpc_parse_response_ok(void **state)
{
    agnc_jsonrpc_message_t message;

    (void)state;

    agnc_jsonrpc_message_init(&message);
    assert_int_equal(
        agnc_jsonrpc_parse_line(
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"tools\":[]}}",
            &message),
        AGNC_STATUS_OK);
    assert_true(message.is_response);
    assert_false(message.has_error);
    assert_non_null(message.result_json);
    assert_string_equal(message.result_json, "{\"tools\":[]}");
    agnc_jsonrpc_message_free(&message);
}

static void test_jsonrpc_parse_response_error(void **state)
{
    agnc_jsonrpc_message_t message;

    (void)state;

    agnc_jsonrpc_message_init(&message);
    assert_int_equal(
        agnc_jsonrpc_parse_line(
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":{\"code\":-32601,\"message\":\"unknown\"}}",
            &message),
        AGNC_STATUS_OK);
    assert_true(message.is_response);
    assert_true(message.has_error);
    assert_int_equal(message.error_code, -32601);
    assert_string_equal(message.error_message, "unknown");
    agnc_jsonrpc_message_free(&message);
}

static void test_jsonrpc_roundtrip_request(void **state)
{
    char *json;
    agnc_jsonrpc_message_t message;

    (void)state;

    json = agnc_jsonrpc_format_request(7, "tools/list", "{}");
    assert_non_null(json);

    agnc_jsonrpc_message_init(&message);
    assert_int_equal(agnc_jsonrpc_parse_line(json, &message), AGNC_STATUS_OK);
    assert_true(message.is_request);
    assert_int_equal(message.id, 7);
    assert_string_equal(message.method, "tools/list");

    agnc_jsonrpc_message_free(&message);
    free(json);
}

static void test_jsonrpc_roundtrip_notification(void **state)
{
    char *json;
    agnc_jsonrpc_message_t message;

    (void)state;

    json = agnc_jsonrpc_format_notification("notifications/initialized", NULL);
    assert_non_null(json);

    agnc_jsonrpc_message_init(&message);
    assert_int_equal(agnc_jsonrpc_parse_line(json, &message), AGNC_STATUS_OK);
    assert_true(message.is_notification);
    assert_string_equal(message.method, "notifications/initialized");

    agnc_jsonrpc_message_free(&message);
    free(json);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_jsonrpc_parse_request),
        cmocka_unit_test(test_jsonrpc_parse_notification),
        cmocka_unit_test(test_jsonrpc_parse_response_ok),
        cmocka_unit_test(test_jsonrpc_parse_response_error),
        cmocka_unit_test(test_jsonrpc_roundtrip_request),
        cmocka_unit_test(test_jsonrpc_roundtrip_notification),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
