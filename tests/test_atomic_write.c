/*
 * test_atomic_write.c
 *
 * Unit test tulis atomik config dan round-trip JSON.
 */

#include "agnc/atomic_write.h"
#include "agnc/config.h"
#include "agnc/path.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static char g_temp_path[1024];

static int setup_temp_file(void **state)
{
    const char *json =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"provider\": {\n"
        "    \"base_url\": \"https://example.test/v1\",\n"
        "    \"model\": \"test/model\",\n"
        "    \"api_key_env\": \"AGNC_API_KEY\"\n"
        "  }\n"
        "}\n";
    FILE *file;
    (void)state;

    if (getcwd(g_temp_path, sizeof(g_temp_path) - 32) == NULL) {
        return -1;
    }

#ifdef _WIN32
    strncat(g_temp_path, "\\agnc_atomic_test.json", sizeof(g_temp_path) - strlen(g_temp_path) - 1);
#else
    strncat(g_temp_path, "/agnc_atomic_test.json", sizeof(g_temp_path) - strlen(g_temp_path) - 1);
#endif

    remove(g_temp_path);
    file = fopen(g_temp_path, "wb");
    if (file == NULL) {
        return -1;
    }
    fputs(json, file);
    fclose(file);
    return 0;
}

static int teardown_temp_file(void **state)
{
    (void)state;
    remove(g_temp_path);
    return 0;
}

static void test_atomic_write_roundtrip(void **state)
{
    const char *updated =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"provider\": {\n"
        "    \"base_url\": \"https://example.test/v1\",\n"
        "    \"model\": \"test/updated\",\n"
        "    \"api_key_env\": \"AGNC_API_KEY\"\n"
        "  },\n"
        "  \"runtime\": { \"verbose\": true }\n"
        "}\n";
    char buffer[512];
    FILE *file;
    size_t read_len;
    (void)state;

    assert_int_equal(agnc_config_save_json(g_temp_path, updated), AGNC_STATUS_OK);

    file = fopen(g_temp_path, "rb");
    assert_non_null(file);
    read_len = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[read_len] = '\0';

    assert_true(strstr(buffer, "test/updated") != NULL);
    assert_true(strstr(buffer, "\"verbose\": true") != NULL);
}

static void test_atomic_write_rejects_invalid_json(void **state)
{
    (void)state;
    assert_int_equal(agnc_config_save_json(g_temp_path, "{not json"), AGNC_STATUS_JSON_ERROR);
}

static void test_config_bootstrap_if_missing(void **state)
{
    char path[1024];
    agnc_config_t config;
    int created = 0;
    (void)state;

    if (getcwd(path, sizeof(path) - 32) == NULL) {
        fail_msg("getcwd failed");
    }

#ifdef _WIN32
    strncat(path, "\\agnc_bootstrap_test.json", sizeof(path) - strlen(path) - 1);
#else
    strncat(path, "/agnc_bootstrap_test.json", sizeof(path) - strlen(path) - 1);
#endif

    remove(path);
    assert_int_equal(agnc_config_bootstrap_if_missing(path, &created), AGNC_STATUS_OK);
    assert_true(created);
    assert_true(agnc_path_exists(path));

    created = 0;
    assert_int_equal(agnc_config_bootstrap_if_missing(path, &created), AGNC_STATUS_OK);
    assert_false(created);

    agnc_config_init(&config);
    assert_int_equal(agnc_config_load(path, &config), AGNC_STATUS_OK);
    assert_string_equal(config.provider_id, "ollama");
    assert_false(config.tui_enabled);
    agnc_config_free(&config);
    remove(path);
}

static void test_config_set_runtime_verbose(void **state)
{
    agnc_config_t config;
    char buffer[512];
    FILE *file;
    size_t read_len;
    (void)state;

    assert_int_equal(agnc_config_set_runtime_verbose(g_temp_path, 1), AGNC_STATUS_OK);

    agnc_config_init(&config);
    assert_int_equal(agnc_config_load(g_temp_path, &config), AGNC_STATUS_OK);
    assert_true(config.verbose);
    agnc_config_free(&config);

    file = fopen(g_temp_path, "rb");
    assert_non_null(file);
    read_len = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[read_len] = '\0';
    assert_true(strstr(buffer, "\"verbose\": true") != NULL);

    assert_int_equal(agnc_config_set_runtime_verbose(g_temp_path, 0), AGNC_STATUS_OK);
    agnc_config_init(&config);
    assert_int_equal(agnc_config_load(g_temp_path, &config), AGNC_STATUS_OK);
    assert_false(config.verbose);
    agnc_config_free(&config);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_atomic_write_roundtrip, setup_temp_file, teardown_temp_file),
        cmocka_unit_test_setup_teardown(test_atomic_write_rejects_invalid_json, setup_temp_file, teardown_temp_file),
        cmocka_unit_test(test_config_bootstrap_if_missing),
        cmocka_unit_test_setup_teardown(test_config_set_runtime_verbose, setup_temp_file, teardown_temp_file),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
