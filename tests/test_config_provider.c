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
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
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
#else
    setenv("AGNC_TEST_API_KEY", "sk-test-key-for-unit-test", 1);
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_load_provider_fixture),
        cmocka_unit_test(test_always_allow_permissions),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
