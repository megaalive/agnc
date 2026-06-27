/*
 * test_tool_cache.c
 *
 * Unit test cache tool in-memory per sesi.
 */

#include "agnc/tool_cache.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_tool_cache_put_get(void **state)
{
    char *result = NULL;
    (void)state;

    agnc_tool_cache_reset();
    assert_int_equal(agnc_tool_cache_put("grep", "{\"pattern\":\"foo\"}", "cached-grep"), AGNC_STATUS_OK);
    assert_true(agnc_tool_cache_get("grep", "{\"pattern\":\"foo\"}", &result));
    assert_non_null(result);
    assert_string_equal(result, "cached-grep");
    free(result);
}

static void test_tool_cache_miss(void **state)
{
    char *result = NULL;
    (void)state;

    agnc_tool_cache_reset();
    assert_false(agnc_tool_cache_get("grep", "{\"pattern\":\"missing\"}", &result));
    assert_null(result);
}

static void test_tool_cache_not_eligible(void **state)
{
    char *result = NULL;
    (void)state;

    agnc_tool_cache_reset();
    assert_false(agnc_tool_cache_is_eligible("shell"));
    assert_int_equal(agnc_tool_cache_put("shell", "{}", "nope"), AGNC_STATUS_OK);
    assert_false(agnc_tool_cache_get("shell", "{}", &result));
}

static void test_tool_cache_reset(void **state)
{
    char *result = NULL;
    (void)state;

    agnc_tool_cache_reset();
    assert_int_equal(agnc_tool_cache_put("glob", "{\"pattern\":\"*.c\"}", "files"), AGNC_STATUS_OK);
    agnc_tool_cache_reset();
    assert_false(agnc_tool_cache_get("glob", "{\"pattern\":\"*.c\"}", &result));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_tool_cache_put_get),
        cmocka_unit_test(test_tool_cache_miss),
        cmocka_unit_test(test_tool_cache_not_eligible),
        cmocka_unit_test(test_tool_cache_reset),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
