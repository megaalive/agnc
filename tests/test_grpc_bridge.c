/*
 * test_grpc_bridge.c
 *
 * Unit test ringan untuk helper grpc_bridge (tanpa jaringan).
 */

#include "agnc/grpc_bridge.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_status_labels(void **state)
{
    (void)state;
    assert_string_equal(agnc_grpc_status_label(AGNC_STATUS_OK), "ok");
    assert_string_equal(agnc_grpc_status_label(AGNC_STATUS_CANCELLED), "cancelled");
    assert_string_equal(agnc_grpc_status_label(AGNC_STATUS_HTTP_ERROR), "http_error");
}

#ifdef _MSC_VER
#define agnc_test_strdup _strdup
#else
#define agnc_test_strdup strdup
#endif

static void test_result_lifecycle(void **state)
{
    agnc_grpc_query_result_t result;

    (void)state;
    agnc_grpc_query_result_init(&result);
    assert_int_equal(result.usage_prompt_tokens, -1);
    assert_int_equal(result.usage_completion_tokens, -1);
    assert_int_equal(result.usage_total_tokens, -1);
    assert_null(result.assistant_text);
    assert_null(result.error_message);

    result.assistant_text = agnc_test_strdup("hello");
    result.error_message = agnc_test_strdup("err");
    agnc_grpc_query_result_free(&result);
    assert_null(result.assistant_text);
    assert_null(result.error_message);
}

static void test_run_query_invalid_args(void **state)
{
    agnc_grpc_query_result_t result;
    agnc_grpc_query_request_t request;

    (void)state;
    memset(&request, 0, sizeof(request));
    agnc_grpc_query_result_init(&result);
    assert_int_equal(agnc_grpc_run_query(NULL, &result), AGNC_STATUS_INVALID_ARGUMENT);
    assert_int_equal(agnc_grpc_run_query(&request, NULL), AGNC_STATUS_INVALID_ARGUMENT);
    request.prompt = "";
    assert_int_equal(agnc_grpc_run_query(&request, &result), AGNC_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_status_labels),
        cmocka_unit_test(test_result_lifecycle),
        cmocka_unit_test(test_run_query_invalid_args),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
