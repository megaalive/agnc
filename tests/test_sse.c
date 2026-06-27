/*
 * test_sse.c
 *
 * Unit test parser SSE/JSON agnc (cmocka).
 */

#include "agnc/net/sse.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

static void test_non_stream_content(void **state)
{
    agnc_sse_parser_t parser;
    const char *payload =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello world\"}}]}";
    (void)state;

    agnc_sse_parser_init(&parser, 0, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, payload, strlen(payload)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_flush(&parser), AGNC_STATUS_OK);
    assert_string_equal(agnc_sse_parser_get_content(&parser), "Hello world");
    assert_false(agnc_sse_parser_has_tool_calls(&parser));
    agnc_sse_parser_free(&parser);
}

static void test_sse_chunked_lines(void **state)
{
    agnc_sse_parser_t parser;
    const char *chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n";
    const char *chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n";
    const char *chunk3 = "data: [DONE]\n\n";
    (void)state;

    agnc_sse_parser_init(&parser, 1, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk1, strlen(chunk1)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk2, strlen(chunk2)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk3, strlen(chunk3)), AGNC_STATUS_OK);
    assert_string_equal(agnc_sse_parser_get_content(&parser), "Hello");
    agnc_sse_parser_free(&parser);
}

static void test_tool_call_non_stream(void **state)
{
    agnc_sse_parser_t parser;
    const char *payload =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"tool_calls\":[{\"id\":\"call_1\","
        "\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}}]}}]}";
    const agnc_sse_tool_call_t *tool_call;
    (void)state;

    agnc_sse_parser_init(&parser, 0, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, payload, strlen(payload)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_flush(&parser), AGNC_STATUS_OK);
    assert_true(agnc_sse_parser_has_tool_calls(&parser));
    assert_int_equal(agnc_sse_parser_get_tool_call_count(&parser), 1);

    tool_call = agnc_sse_parser_get_tool_call(&parser, 0);
    assert_non_null(tool_call);
    assert_string_equal(tool_call->name, "read_file");
    assert_non_null(tool_call->arguments);
    assert_string_equal(tool_call->arguments, "{\"path\":\"README.md\"}");
    agnc_sse_parser_free(&parser);
}

static void test_split_chunk_boundary(void **state)
{
    agnc_sse_parser_t parser;
    const char *part1 = "data: {\"choices\":[{\"delta\":{\"content\":\"";
    const char *part2 = "split test\"}}]}\n";
    (void)state;

    agnc_sse_parser_init(&parser, 1, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, part1, strlen(part1)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, part2, strlen(part2)), AGNC_STATUS_OK);
    assert_string_equal(agnc_sse_parser_get_content(&parser), "split test");
    agnc_sse_parser_free(&parser);
}

static void test_provider_error_payload(void **state)
{
    agnc_sse_parser_t parser;
    const char *payload = "{\"error\":{\"message\":\"rate limit exceeded\"}}";
    (void)state;

    agnc_sse_parser_init(&parser, 0, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, payload, strlen(payload)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_flush(&parser), AGNC_STATUS_PROVIDER_ERROR);
    assert_string_equal(agnc_sse_parser_get_error(&parser), "rate limit exceeded");
    agnc_sse_parser_free(&parser);
}

static void test_reasoning_separate_from_content(void **state)
{
    agnc_sse_parser_t parser;
    const char *payload =
        "{\"choices\":[{\"message\":{\"content\":\"Jawaban.\",\"reasoning\":\"Internal monolog.\"}}]}";
    (void)state;

    agnc_sse_parser_init(&parser, 0, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, payload, strlen(payload)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_flush(&parser), AGNC_STATUS_OK);
    assert_non_null(agnc_sse_parser_get_content(&parser));
    assert_string_equal(agnc_sse_parser_get_content(&parser), "Jawaban.");
    assert_non_null(agnc_sse_parser_get_reasoning(&parser));
    assert_string_equal(agnc_sse_parser_get_reasoning(&parser), "Internal monolog.");
    agnc_sse_parser_free(&parser);
}

static void test_tool_arguments_finalize_path(void **state)
{
#ifdef _MSC_VER
    char *raw = _strdup("{\"path\":\"README.md\"}");
#else
    char *raw = strdup("{\"path\":\"README.md\"}");
#endif
    char *fixed;
    (void)state;

    fixed = agnc_sse_tool_arguments_finalize(raw);
    assert_non_null(fixed);
    assert_string_equal(fixed, "{\"path\":\"README.md\"}");
    free(fixed);
}

static void test_tool_arguments_finalize_fragment(void **state)
{
#ifdef _MSC_VER
    char *raw = _strdup("{\"path\":\"roadmap.md\"");
#else
    char *raw = strdup("{\"path\":\"roadmap.md\"");
#endif
    char *fixed;
    (void)state;

    fixed = agnc_sse_tool_arguments_finalize(raw);
    assert_non_null(fixed);
    assert_string_equal(fixed, "{\"path\":\"roadmap.md\"}");
    free(fixed);
}

static void test_sse_incremental_mid_word(void **state)
{
    agnc_sse_parser_t parser;
    const char *chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Bandung adalah ibu\"}}]}\n\n";
    const char *chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\" kota Provinsi Jawa Barat.\"}}]}\n\n";
    (void)state;

    agnc_sse_parser_init(&parser, 1, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk1, strlen(chunk1)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk2, strlen(chunk2)), AGNC_STATUS_OK);
    assert_string_equal(
        agnc_sse_parser_get_content(&parser), "Bandung adalah ibu kota Provinsi Jawa Barat.");
    agnc_sse_parser_free(&parser);
}

static void test_sse_incremental_deltas(void **state)
{
    agnc_sse_parser_t parser;
    const char *chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Jakar\"}}]}\n\n";
    const char *chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\"ta adalah\"}}]}\n\n";
    const char *chunk3 = "data: {\"choices\":[{\"delta\":{\"content\":\" kota Indonesia.\"}}]}\n\n";
    (void)state;

    agnc_sse_parser_init(&parser, 1, 0);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk1, strlen(chunk1)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk2, strlen(chunk2)), AGNC_STATUS_OK);
    assert_int_equal(agnc_sse_parser_feed(&parser, chunk3, strlen(chunk3)), AGNC_STATUS_OK);
    assert_string_equal(agnc_sse_parser_get_content(&parser), "Jakarta adalah kota Indonesia.");
    agnc_sse_parser_free(&parser);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_non_stream_content),
        cmocka_unit_test(test_sse_chunked_lines),
        cmocka_unit_test(test_sse_incremental_deltas),
        cmocka_unit_test(test_sse_incremental_mid_word),
        cmocka_unit_test(test_tool_call_non_stream),
        cmocka_unit_test(test_split_chunk_boundary),
        cmocka_unit_test(test_provider_error_payload),
        cmocka_unit_test(test_reasoning_separate_from_content),
        cmocka_unit_test(test_tool_arguments_finalize_path),
        cmocka_unit_test(test_tool_arguments_finalize_fragment),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
