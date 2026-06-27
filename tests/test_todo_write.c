/*
 * test_todo_write.c
 *
 * Unit test tool todo_write: validasi argumen dan tulis todos.json.
 */

#include "agnc/tool.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_todo_write_rejects_invalid_json(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    status = agnc_tool_todo_write_execute("not-json", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_FAILED);
    assert_non_null(result);
    assert_non_null(strstr(result, "invalid JSON"));
    free(result);
}

static void test_todo_write_requires_todos_array(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    status = agnc_tool_todo_write_execute("{\"merge\":true}", &result);
    assert_int_equal(status, AGNC_STATUS_TOOL_FAILED);
    assert_non_null(result);
    assert_non_null(strstr(result, "todos"));
    free(result);
}

static void test_todo_write_saves_array(void **state)
{
    char *result = NULL;
    agnc_status_t status;
    const char *args =
        "{\"todos\":[{\"id\":\"1\",\"content\":\"test item\",\"status\":\"pending\"}]}";
    (void)state;

    status = agnc_tool_todo_write_execute(args, &result);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(result);
    assert_non_null(strstr(result, "ok:"));
    free(result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_todo_write_rejects_invalid_json),
        cmocka_unit_test(test_todo_write_requires_todos_array),
        cmocka_unit_test(test_todo_write_saves_array),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
