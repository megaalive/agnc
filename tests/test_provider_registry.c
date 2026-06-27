/*
 * test_provider_registry.c
 *
 * Unit test registry gateway hasil generate_integrations.py.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "agnc/provider.h"
#include "agnc/opencode.h"
#include "agnc/status.h"

static void test_registry_count(void **state)
{
    (void)state;
    assert_true(agnc_registry_gateway_count() >= 6);
}

static void test_find_openrouter(void **state)
{
    const agnc_gateway_descriptor_t *gateway;

    (void)state;
    gateway = agnc_registry_find_gateway("openrouter");
    assert_non_null(gateway);
    assert_string_equal(gateway->id, "openrouter");
    assert_string_equal(gateway->default_base_url, "https://openrouter.ai/api/v1");
    assert_int_equal(gateway->transport_kind, AGNC_TRANSPORT_OPENAI_COMPATIBLE);
    assert_true(gateway->requires_auth);
}

static void test_find_unknown(void **state)
{
    (void)state;
    assert_null(agnc_registry_find_gateway("not-a-real-gateway"));
}

static void test_build_chat_url(void **state)
{
    const agnc_gateway_descriptor_t *gateway = agnc_registry_find_gateway("openrouter");
    char *url;

    (void)state;
    assert_non_null(gateway);
    url = agnc_provider_build_chat_url(gateway, "https://openrouter.ai/api/v1/");
    assert_non_null(url);
    assert_string_equal(url, "https://openrouter.ai/api/v1/chat/completions");
    free(url);
}

static void test_resolve_catalog_model(void **state)
{
    const agnc_gateway_descriptor_t *gateway = agnc_registry_find_gateway("openrouter");
    const char *api_name;

    (void)state;
    assert_non_null(gateway);
    api_name = agnc_provider_resolve_api_model(gateway, "owl-alpha");
    assert_string_equal(api_name, "openrouter/owl-alpha");
}

static void test_find_ollama(void **state)
{
    const agnc_gateway_descriptor_t *gateway;
    char *url;

    (void)state;
    gateway = agnc_registry_find_gateway("ollama");
    assert_non_null(gateway);
    assert_string_equal(gateway->default_base_url, "http://127.0.0.1:11434/v1");
    assert_int_equal(gateway->requires_auth, 0);
    assert_int_equal(gateway->model_count, 0);

    url = agnc_provider_build_chat_url(gateway, "http://127.0.0.1:11434/v1");
    assert_non_null(url);
    assert_string_equal(url, "http://127.0.0.1:11434/v1/chat/completions");
    free(url);
}

static void test_find_opencode(void **state)
{
    const agnc_gateway_descriptor_t *gateway;

    (void)state;
    gateway = agnc_registry_find_gateway("opencode-local");
    assert_non_null(gateway);
    assert_string_equal(gateway->default_base_url, "http://127.0.0.1:4096");
    assert_int_equal(gateway->transport_kind, AGNC_TRANSPORT_OPENCODE_NATIVE);
    assert_int_equal(gateway->requires_auth, 0);
    assert_int_equal(gateway->supports_tool_calls, 0);
}

static void test_opencode_parse_model(void **state)
{
    char *provider_id = NULL;
    char *model_id = NULL;

    (void)state;
    assert_int_equal(
        agnc_opencode_parse_model("opencode/big-pickle", &provider_id, &model_id),
        AGNC_STATUS_OK);
    assert_string_equal(provider_id, "opencode");
    assert_string_equal(model_id, "big-pickle");
    free(provider_id);
    free(model_id);

    provider_id = NULL;
    model_id = NULL;
    assert_int_equal(agnc_opencode_parse_model("big-pickle", &provider_id, &model_id), AGNC_STATUS_OK);
    assert_string_equal(provider_id, "opencode");
    assert_string_equal(model_id, "big-pickle");
    free(provider_id);
    free(model_id);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_registry_count),
        cmocka_unit_test(test_find_openrouter),
        cmocka_unit_test(test_find_unknown),
        cmocka_unit_test(test_build_chat_url),
        cmocka_unit_test(test_resolve_catalog_model),
        cmocka_unit_test(test_find_ollama),
        cmocka_unit_test(test_find_opencode),
        cmocka_unit_test(test_opencode_parse_model),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
