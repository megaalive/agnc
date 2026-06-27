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

static void test_shell_blocks_findstr(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    assert_true(agnc_tool_shell_is_search_command("findstr /s /i agnc_query src\\*") != 0);
    assert_true(agnc_tool_shell_is_search_command("where rg") != 0);
    assert_true(agnc_tool_shell_is_search_command("rg agnc_query src") != 0);
    assert_true(agnc_tool_shell_is_search_command("dir src") != 0);
    assert_true(agnc_tool_shell_is_search_command("dir") == 0);

    status = agnc_tool_shell_execute("{\"command\":\"findstr /s /i agnc_query src\\\\*\"}", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_FAILED);
    assert_non_null(result);
    assert_non_null(strstr(result, "grep tool"));
    free(result);
}

static void test_shell_blocks_dangerous_commands(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    assert_true(agnc_tool_shell_is_dangerous_command("rm -rf /") != 0);
    assert_true(agnc_tool_shell_is_dangerous_command("format c:") != 0);
    assert_true(agnc_tool_shell_is_dangerous_command("diskpart") != 0);
    assert_true(agnc_tool_shell_is_dangerous_command("shutdown /s /t 0") != 0);
    assert_true(agnc_tool_shell_is_dangerous_command("echo hello") == 0);

    status = agnc_tool_shell_execute("{\"command\":\"rm -rf /\"}", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_DENIED);
    assert_non_null(result);
    assert_non_null(strstr(result, "dangerous shell command blocked"));
    free(result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_shell_truncates_large_output),
        cmocka_unit_test(test_shell_blocks_findstr),
        cmocka_unit_test(test_shell_blocks_dangerous_commands),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
