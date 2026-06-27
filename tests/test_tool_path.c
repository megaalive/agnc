/*
 * test_tool_path.c
 *
 * Unit test resolusi path dan validasi workspace tool.
 */

#include "agnc/rg_locate.h"
#include "agnc/tool.h"
#include "agnc/tool_path.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifdef _WIN32
#include <direct.h>
#define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

static void test_path_rejects_dotdot(void **state)
{
    char *resolved = NULL;
    (void)state;

    assert_int_equal(agnc_tool_path_resolve("../secret.txt", &resolved), AGNC_STATUS_TOOL_DENIED);
    assert_null(resolved);
}

static void test_path_validate_workspace(void **state)
{
    char *resolved = NULL;
    agnc_status_t status;
    (void)state;

    status = agnc_tool_path_resolve("README.md", &resolved);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(resolved);

    status = agnc_tool_path_validate_workspace(resolved);
    assert_int_equal(status, AGNC_STATUS_OK);

    free(resolved);
    resolved = NULL;

    /* Path di luar workspace harus ditolak. */
    status = agnc_tool_path_resolve("C:\\Windows\\System32\\drivers\\etc\\hosts", &resolved);
    if (status == AGNC_STATUS_OK && resolved != NULL) {
        assert_int_equal(agnc_tool_path_validate_workspace(resolved), AGNC_STATUS_TOOL_DENIED);
        free(resolved);
    }
}

static void test_write_and_edit_roundtrip(void **state)
{
    char *resolved = NULL;
    const char *json_write =
        "{\"path\":\"agnc_fase2_test.txt\",\"content\":\"hello agnc\\nline two\\n\"}";
    const char *json_edit =
        "{\"path\":\"agnc_fase2_test.txt\",\"old_string\":\"line two\",\"new_string\":\"line edited\"}";
    char *result = NULL;
    FILE *file;
    char buffer[128];
    (void)state;

    assert_int_equal(agnc_tool_path_resolve("agnc_fase2_test.txt", &resolved), AGNC_STATUS_OK);
    free(resolved);
    resolved = NULL;

    remove("agnc_fase2_test.txt");
    remove("../../agnc_fase2_test.txt");
    remove("../../../agnc_fase2_test.txt");
    remove("../../../../agnc_fase2_test.txt");

    assert_int_equal(agnc_tool_write_file_execute(json_write, &result), AGNC_STATUS_OK);
    assert_non_null(result);
    assert_true(strstr(result, "ok") != NULL);
    free(result);
    result = NULL;

    assert_int_equal(agnc_tool_edit_file_execute(json_edit, &result), AGNC_STATUS_OK);
    assert_non_null(result);
    free(result);
    result = NULL;

    file = fopen("agnc_fase2_test.txt", "rb");
    if (file == NULL) {
        file = fopen("../../../../agnc_fase2_test.txt", "rb");
    }
    assert_non_null(file);
    assert_non_null(fgets(buffer, sizeof(buffer), file));
    assert_string_equal(buffer, "hello agnc\n");
    assert_non_null(fgets(buffer, sizeof(buffer), file));
    assert_string_equal(buffer, "line edited\n");
    fclose(file);

    remove("agnc_fase2_test.txt");
    remove("../../../../agnc_fase2_test.txt");
}

static void test_grep_finds_in_src(void **state)
{
    const char *json = "{\"pattern\":\"agnc_query\"}";
    char *result = NULL;
    agnc_status_t status;
    (void)state;

    assert_non_null(agnc_rg_locate_binary());

    status = agnc_tool_grep_execute(json, &result);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(result);
    assert_true(strstr(result, "agnc_query") != NULL);
    free(result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_path_rejects_dotdot),
        cmocka_unit_test(test_path_validate_workspace),
        cmocka_unit_test(test_write_and_edit_roundtrip),
        cmocka_unit_test(test_grep_finds_in_src),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
