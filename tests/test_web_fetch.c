/*
 * test_web_fetch.c
 *
 * Unit test tool web_fetch: preview URL dan validasi argumen (tanpa jaringan).
 */

#include "agnc/tool.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_web_fetch_url_preview(void **state)
{
    const char *preview;
    (void)state;

    preview = agnc_tool_web_fetch_url_preview("{\"url\":\"https://example.com/path\"}");
    assert_non_null(preview);
    assert_string_equal(preview, "https://example.com/path");
}

static void test_web_fetch_rejects_bad_url(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    status = agnc_tool_web_fetch_execute("{\"url\":\"ftp://example.com\"}", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_FAILED);
    assert_non_null(result);
    assert_non_null(strstr(result, "http"));
    free(result);
}

static void test_web_fetch_requires_url(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    status = agnc_tool_web_fetch_execute("{}", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_FAILED);
    assert_non_null(result);
    assert_non_null(strstr(result, "url"));
    free(result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_web_fetch_url_preview),
        cmocka_unit_test(test_web_fetch_rejects_bad_url),
        cmocka_unit_test(test_web_fetch_requires_url),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
