/*
 * sse.h
 *
 * Parser respons SSE/JSON OpenAI-compatible untuk streaming dan non-stream.
 */

#ifndef AGNC_NET_SSE_H
#define AGNC_NET_SSE_H

#include "agnc/status.h"

#include <stddef.h>

#define AGNC_SSE_MAX_TOOL_CALLS 8

typedef struct {
    char *id;
    char *name;
    char *arguments;
} agnc_sse_tool_call_t;

typedef struct {
    char *line_buffer;
    size_t line_length;
    size_t line_capacity;
    char *last_content_chunk;
    char *last_reasoning_chunk;
    char *last_error;
    agnc_sse_tool_call_t tool_calls[AGNC_SSE_MAX_TOOL_CALLS];
    size_t tool_call_count;
    int has_tool_calls;
    int printed_any;
    int stream_mode;
    int verbose;
} agnc_sse_parser_t;

void agnc_sse_parser_init(agnc_sse_parser_t *parser, int stream_mode, int verbose);
void agnc_sse_parser_free(agnc_sse_parser_t *parser);

agnc_status_t agnc_sse_parser_feed(agnc_sse_parser_t *parser, const char *chunk, size_t length);
agnc_status_t agnc_sse_parser_flush(agnc_sse_parser_t *parser);

/* Cetak content ke stdout; reasoning ke stderr hanya jika verbose. */
void agnc_sse_parser_finalize_turn(agnc_sse_parser_t *parser);

const char *agnc_sse_parser_get_content(const agnc_sse_parser_t *parser);
const char *agnc_sse_parser_get_reasoning(const agnc_sse_parser_t *parser);
const char *agnc_sse_parser_get_error(const agnc_sse_parser_t *parser);
int agnc_sse_parser_has_tool_calls(const agnc_sse_parser_t *parser);
size_t agnc_sse_parser_get_tool_call_count(const agnc_sse_parser_t *parser);
const agnc_sse_tool_call_t *agnc_sse_parser_get_tool_call(const agnc_sse_parser_t *parser, size_t index);
int agnc_sse_parser_printed_any(const agnc_sse_parser_t *parser);

/* Perbaiki arguments tool yang dirakit dari fragmen stream model. */
char *agnc_sse_tool_arguments_finalize(char *raw);

#endif
