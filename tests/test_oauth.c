/*
 * test_oauth.c
 *
 * Unit test refresh OAuth (parse + kebijakan expiry) tanpa jaringan live.
 */

#include "agnc/oauth.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifdef _MSC_VER
#define agnc_test_strdup _strdup
#else
#define agnc_test_strdup strdup
#endif

static void test_seconds_until_expiry_unset(void **state)
{
    agnc_oauth_token_t token;

    (void)state;
    agnc_oauth_token_init(&token);
    assert_int_equal(agnc_oauth_token_seconds_until_expiry_at(&token, 1000), -1);
    token.expires_at = 1500;
    assert_int_equal(agnc_oauth_token_seconds_until_expiry_at(&token, 1000), 500);
    agnc_oauth_token_free(&token);
}

static void test_needs_refresh_policy(void **state)
{
    agnc_oauth_token_t token;

    (void)state;
    agnc_oauth_token_init(&token);
    token.refresh_token = agnc_test_strdup("refresh");
    token.expires_at = 2000;

    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 1000, 0), 0);
    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 1881, 0), 1);
    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 2100, 0), 1);
    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 1000, 1), 1);

    token.expires_at = 0;
    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 1000, 0), 0);
    assert_int_equal(agnc_oauth_token_needs_refresh_at(&token, 1000, 1), 1);

    agnc_oauth_token_free(&token);
}

static void test_parse_refresh_response(void **state)
{
    const char *json =
        "{"
        "\"access_token\":\"new-access\","
        "\"refresh_token\":\"new-refresh\","
        "\"expires_in\":3600"
        "}";
    agnc_oauth_token_t previous;
    agnc_oauth_token_t updated;

    (void)state;
    agnc_oauth_token_init(&previous);
    previous.refresh_token = agnc_test_strdup("old-refresh");

    agnc_oauth_token_init(&updated);
    assert_int_equal(
        agnc_oauth_parse_refresh_response(json, &previous, &updated, 1000),
        AGNC_STATUS_OK);
    assert_string_equal(updated.access_token, "new-access");
    assert_string_equal(updated.refresh_token, "new-refresh");
    assert_int_equal(updated.expires_at, 4600);

    agnc_oauth_token_free(&previous);
    agnc_oauth_token_free(&updated);
}

static void test_parse_refresh_keeps_old_refresh(void **state)
{
    const char *json = "{\"access_token\":\"new-access\",\"expires_in\":60}";
    agnc_oauth_token_t previous;
    agnc_oauth_token_t updated;

    (void)state;
    agnc_oauth_token_init(&previous);
    previous.refresh_token = agnc_test_strdup("keep-me");

    agnc_oauth_token_init(&updated);
    assert_int_equal(
        agnc_oauth_parse_refresh_response(json, &previous, &updated, 500),
        AGNC_STATUS_OK);
    assert_string_equal(updated.access_token, "new-access");
    assert_string_equal(updated.refresh_token, "keep-me");
    assert_int_equal(updated.expires_at, 560);

    agnc_oauth_token_free(&previous);
    agnc_oauth_token_free(&updated);
}

static void test_parse_refresh_invalid_json(void **state)
{
    agnc_oauth_token_t updated;

    (void)state;
    agnc_oauth_token_init(&updated);
    assert_int_equal(
        agnc_oauth_parse_refresh_response("{\"token\":\"x\"}", NULL, &updated, 0),
        AGNC_STATUS_JSON_ERROR);
    agnc_oauth_token_free(&updated);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_seconds_until_expiry_unset),
        cmocka_unit_test(test_needs_refresh_policy),
        cmocka_unit_test(test_parse_refresh_response),
        cmocka_unit_test(test_parse_refresh_keeps_old_refresh),
        cmocka_unit_test(test_parse_refresh_invalid_json),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
