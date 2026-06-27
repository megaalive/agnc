/*
 * test_config_provider.c
 *
 * Unit test resolusi provider.active + providers{} + descriptor gateway.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "agnc/config.h"

#ifndef AGNC_SOURCE_DIR
#define AGNC_SOURCE_DIR "."
#endif

#ifdef _WIN32
#include <stdlib.h>
#endif

static void test_load_provider_fixture(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char fixture_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
    _putenv_s("AGNC_MODEL", "");
    _putenv_s("AGNC_PROVIDER", "");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
    unsetenv("AGNC_MODEL");
    unsetenv("AGNC_PROVIDER");
#endif

    snprintf(fixture_path, sizeof(fixture_path), "%s/tests/fixtures/provider_test.json", AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load(fixture_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_string_equal(config.provider_id, "openrouter");
    assert_string_equal(config.gateway_id, "openrouter");
    assert_string_equal(config.model, "openrouter/owl-alpha");
    assert_non_null(config.base_url);
    assert_non_null(config.api_key);
    assert_string_equal(config.api_key, "sk-test-key-for-unit-test");

    agnc_config_free(&config);
}

static void test_always_allow_permissions(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char fixture_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
    _putenv_s("AGNC_MODEL", "");
    _putenv_s("AGNC_PROVIDER", "");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
    unsetenv("AGNC_MODEL");
    unsetenv("AGNC_PROVIDER");
#endif

    snprintf(
        fixture_path,
        sizeof(fixture_path),
        "%s/tests/fixtures/permissions_always_allow.json",
        AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load(fixture_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(config.ask_shell_permission, 0);
    assert_int_equal(config.ask_write_permission, 0);
    assert_int_equal(config.ask_mcp_permission, 0);
    assert_int_equal(config.ask_web_fetch_permission, 0);
    assert_int_equal(config.tool_web_fetch, 1);
    assert_int_equal(config.tool_todo_write, 1);

    agnc_config_free(&config);
}

static void test_always_deny_permissions(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char fixture_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
    _putenv_s("AGNC_MODEL", "");
    _putenv_s("AGNC_PROVIDER", "");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
    unsetenv("AGNC_MODEL");
    unsetenv("AGNC_PROVIDER");
#endif

    snprintf(
        fixture_path,
        sizeof(fixture_path),
        "%s/tests/fixtures/permissions_always_deny.json",
        AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load(fixture_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(config.deny_shell_permission, 1);
    assert_int_equal(config.deny_write_permission, 1);
    assert_int_equal(config.deny_mcp_permission, 1);
    assert_int_equal(config.deny_web_fetch_permission, 1);
    assert_int_equal(config.ask_shell_permission, 0);
    assert_int_equal(config.ask_write_permission, 0);
    assert_int_equal(config.ask_mcp_permission, 0);
    assert_int_equal(config.ask_web_fetch_permission, 0);

    agnc_config_free(&config);
}

static void agnc_test_path_to_json(char *path, size_t path_size)
{
    size_t index;

    for (index = 0; index < path_size && path[index] != '\0'; index++) {
        if (path[index] == '\\') {
            path[index] = '/';
        }
    }
}

static agnc_status_t agnc_test_write_config_file(
    const char *path,
    const char *active,
    const char *provider_id,
    const char *api_key_file)
{
    FILE *file;

    file = fopen(path, "wb");
    if (file == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    fprintf(
        file,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"provider\": { \"active\": \"%s\", \"model\": \"test-model\" },\n"
        "  \"providers\": {\n"
        "    \"%s\": {\n"
        "      \"gateway\": \"%s\",\n"
        "      \"default_model\": \"test-model\",\n"
        "      \"api_key_file\": \"%s\"\n"
        "    }\n"
        "  },\n"
        "  \"runtime\": { \"stream\": false }\n"
        "}\n",
        active,
        provider_id,
        provider_id,
        api_key_file);

    fclose(file);
    return AGNC_STATUS_OK;
}

static void test_api_key_file_openrouter_first_line(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char key_path[512];
    char config_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_MODEL", "");
    _putenv_s("AGNC_PROVIDER", "");
    _putenv_s("AGNC_TEST_API_KEY", "");
    _putenv_s("AGNC_API_KEY", "");
#else
    unsetenv("AGNC_MODEL");
    unsetenv("AGNC_PROVIDER");
    unsetenv("AGNC_TEST_API_KEY");
    unsetenv("AGNC_API_KEY");
#endif

    snprintf(key_path, sizeof(key_path), "%s/tests/fixtures/keys/openrouter.txt", AGNC_SOURCE_DIR);
    agnc_test_path_to_json(key_path, sizeof(key_path));

    snprintf(config_path, sizeof(config_path), "%s/tests/fixtures/tmp_openrouter_key.json", AGNC_SOURCE_DIR);
    agnc_test_path_to_json(config_path, sizeof(config_path));
    assert_int_equal(
        agnc_test_write_config_file(config_path, "openrouter", "openrouter", key_path),
        AGNC_STATUS_OK);

    agnc_config_init(&config);
    status = agnc_config_load(config_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(config.api_key);
    assert_string_equal(config.api_key, "sk-or-v1-test-key-from-file");

    agnc_config_free(&config);
    remove(config_path);
}

static void test_api_key_file_gemini_first_line(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char key_path[512];
    char config_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_MODEL", "");
    _putenv_s("AGNC_PROVIDER", "");
    _putenv_s("AGNC_TEST_API_KEY", "");
    _putenv_s("AGNC_API_KEY", "");
    _putenv_s("GEMINI_API_KEY", "");
#else
    unsetenv("AGNC_MODEL");
    unsetenv("AGNC_PROVIDER");
    unsetenv("AGNC_TEST_API_KEY");
    unsetenv("AGNC_API_KEY");
    unsetenv("GEMINI_API_KEY");
#endif

    snprintf(key_path, sizeof(key_path), "%s/tests/fixtures/keys/gemini.txt", AGNC_SOURCE_DIR);
    agnc_test_path_to_json(key_path, sizeof(key_path));

    snprintf(config_path, sizeof(config_path), "%s/tests/fixtures/tmp_gemini_key.json", AGNC_SOURCE_DIR);
    agnc_test_path_to_json(config_path, sizeof(config_path));
    assert_int_equal(
        agnc_test_write_config_file(config_path, "gemini", "gemini", key_path),
        AGNC_STATUS_OK);

    agnc_config_init(&config);
    status = agnc_config_load(config_path, &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_non_null(config.api_key);
    assert_string_equal(config.api_key, "AIzaSyTestGeminiKeyFromFile");

    agnc_config_free(&config);
    remove(config_path);
}

static void test_list_provider_ids(void **state)
{
    char fixture_path[512];
    char **ids = NULL;
    size_t count = 0;
    agnc_status_t status;

    (void)state;

    snprintf(fixture_path, sizeof(fixture_path), "%s/tests/fixtures/providers_multi.json", AGNC_SOURCE_DIR);
    status = agnc_config_list_provider_ids(fixture_path, &ids, &count);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(count, 2);
    assert_non_null(ids[0]);
    assert_non_null(ids[1]);

    agnc_config_free_provider_id_list(ids, count);
}

static void test_load_provider_entry_ignores_global_env(void **state)
{
    agnc_config_t config;
    agnc_status_t status;
    char fixture_path[512];

    (void)state;

#ifdef _WIN32
    _putenv_s("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test");
    _putenv_s("AGNC_BASE_URL", "http://127.0.0.1:9999/v1");
    _putenv_s("AGNC_MODEL", "wrong/model");
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
    setenv("AGNC_BASE_URL", "http://127.0.0.1:9999/v1", 1);
    setenv("AGNC_MODEL", "wrong/model", 1);
#endif

    snprintf(fixture_path, sizeof(fixture_path), "%s/tests/fixtures/providers_multi.json", AGNC_SOURCE_DIR);

    agnc_config_init(&config);
    status = agnc_config_load_provider_entry(fixture_path, "ollama", &config);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_string_equal(config.provider_id, "ollama");
    assert_string_equal(config.gateway_id, "ollama");
    assert_string_equal(config.base_url, "http://127.0.0.1:11434/v1");
    assert_string_equal(config.model, "llama3.2");

    agnc_config_free(&config);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_load_provider_fixture),
        cmocka_unit_test(test_always_allow_permissions),
        cmocka_unit_test(test_always_deny_permissions),
        cmocka_unit_test(test_api_key_file_openrouter_first_line),
        cmocka_unit_test(test_api_key_file_gemini_first_line),
        cmocka_unit_test(test_list_provider_ids),
        cmocka_unit_test(test_load_provider_entry_ignores_global_env),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
