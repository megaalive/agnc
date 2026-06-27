/*
 * test_hooks.c
 *
 * Unit test hook runner dan payload JSON.
 */

#include "agnc/config.h"
#include "agnc/hooks.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_hooks_disabled(void **state)
{
    agnc_config_t config;
    int blocked = 0;
    (void)state;

    memset(&config, 0, sizeof(config));
    config.hooks_enabled = 0;

    assert_int_equal(
        agnc_hooks_run(&config, AGNC_HOOK_EVENT_PRE_TOOL, "{}", &blocked),
        AGNC_STATUS_OK);
    assert_int_equal(blocked, 0);
}

static void test_hooks_build_payload(void **state)
{
    agnc_hook_payload_input_t input;
    char *json = NULL;
    (void)state;

    memset(&input, 0, sizeof(input));
    input.session_name = "current";
    input.tool_name = "read_file";
    input.tool_arguments = "{\"path\":\"README.md\"}";

    json = agnc_hooks_build_payload_json(AGNC_HOOK_EVENT_PRE_TOOL, &input);
    assert_non_null(json);
    assert_true(strstr(json, "pre_tool") != NULL);
    assert_true(strstr(json, "read_file") != NULL);
    agnc_hooks_free_payload(json);
}

static void test_hooks_run_exit_zero(void **state)
{
    agnc_config_t config;
    const char *commands[] = {"exit /b 0"};
    (void)state;

    memset(&config, 0, sizeof(config));
    config.hooks_enabled = 1;
    config.hooks_pre_turn = (char **)commands;
    config.hooks_pre_turn_count = 1;

    assert_int_equal(
        agnc_hooks_run(&config, AGNC_HOOK_EVENT_PRE_TURN, "{\"event\":\"pre_turn\"}", NULL),
        AGNC_STATUS_OK);
}

static void test_hooks_pre_tool_blocks(void **state)
{
    agnc_config_t config;
    const char *commands[] = {"exit /b 1"};
    int blocked = 0;
    (void)state;

    memset(&config, 0, sizeof(config));
    config.hooks_enabled = 1;
    config.hooks_pre_tool = (char **)commands;
    config.hooks_pre_tool_count = 1;

    assert_int_equal(
        agnc_hooks_run(&config, AGNC_HOOK_EVENT_PRE_TOOL, "{}", &blocked),
        AGNC_STATUS_TOOL_DENIED);
    assert_int_equal(blocked, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hooks_disabled),
        cmocka_unit_test(test_hooks_build_payload),
        cmocka_unit_test(test_hooks_run_exit_zero),
        cmocka_unit_test(test_hooks_pre_tool_blocks),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
