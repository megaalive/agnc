/*
 * test_shell.c
 *
 * Unit test tool shell: output besar harus terpotong dengan aman.
 */

#include "agnc/tool.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_shell_truncates_large_output(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    size_t length;
    (void)state;

#ifdef _WIN32
    status = agnc_tool_shell_execute("{\"command\":\"1..15000 | ForEach-Object { $_ }\"}", &result);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(result);
    length = strlen(result);
    assert_true(length > 0);
    assert_true(length < 128 * 1024);
    assert_non_null(strstr(result, "(output truncated)"));
    free(result);
#else
    skip();
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_shell_truncates_large_output),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
