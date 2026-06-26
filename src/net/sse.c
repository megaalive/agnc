/*
 * sse.c
 *
 * Parser baris SSE dan JSON utuh dari endpoint chat/completions OpenAI-compatible.
 */

#include "agnc/net/sse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_LINE_BUFFER_INITIAL 4096

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_sse_parser_init(agnc_sse_parser_t *parser, int stream_mode, int verbose)
{
    memset(parser, 0, sizeof(*parser));
    parser->stream_mode = stream_mode;
    parser->verbose = verbose;
}

void agnc_sse_parser_free(agnc_sse_parser_t *parser)
{
    size_t index;

    free(parser->line_buffer);
    free(parser->last_content_chunk);
    free(parser->last_reasoning_chunk);
    free(parser->last_error);
    parser->line_buffer = NULL;
    parser->last_content_chunk = NULL;
    parser->last_reasoning_chunk = NULL;
    parser->last_error = NULL;
    parser->printed_any = 0;
    parser->line_length = 0;
    parser->line_capacity = 0;

    for (index = 0; index < parser->tool_call_count; index++) {
        free(parser->tool_calls[index].id);
        free(parser->tool_calls[index].name);
        free(parser->tool_calls[index].arguments);
        parser->tool_calls[index].id = NULL;
        parser->tool_calls[index].name = NULL;
        parser->tool_calls[index].arguments = NULL;
    }
    parser->tool_call_count = 0;
    parser->has_tool_calls = 0;
}

const char *agnc_sse_parser_get_content(const agnc_sse_parser_t *parser)
{
    return parser->last_content_chunk;
}

const char *agnc_sse_parser_get_reasoning(const agnc_sse_parser_t *parser)
{
    return parser->last_reasoning_chunk;
}

const char *agnc_sse_parser_get_error(const agnc_sse_parser_t *parser)
{
    return parser->last_error;
}

int agnc_sse_parser_has_tool_calls(const agnc_sse_parser_t *parser)
{
    return parser->has_tool_calls;
}

size_t agnc_sse_parser_get_tool_call_count(const agnc_sse_parser_t *parser)
{
    return parser->tool_call_count;
}

const agnc_sse_tool_call_t *agnc_sse_parser_get_tool_call(const agnc_sse_parser_t *parser, size_t index)
{
    if (index >= parser->tool_call_count) {
        return NULL;
    }
    return &parser->tool_calls[index];
}

int agnc_sse_parser_printed_any(const agnc_sse_parser_t *parser)
{
    return parser->printed_any;
}

static agnc_status_t agnc_sse_append_line_buffer(agnc_sse_parser_t *parser, const char *data, size_t length)
{
    if (parser->line_length + length + 1 > parser->line_capacity) {
        size_t new_capacity = parser->line_capacity == 0 ? AGNC_LINE_BUFFER_INITIAL : parser->line_capacity * 2;
        char *new_buffer;

        while (new_capacity < parser->line_length + length + 1) {
            new_capacity *= 2;
        }

        new_buffer = (char *)realloc(parser->line_buffer, new_capacity);
        if (new_buffer == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        parser->line_buffer = new_buffer;
        parser->line_capacity = new_capacity;
    }

    memcpy(parser->line_buffer + parser->line_length, data, length);
    parser->line_length += length;
    parser->line_buffer[parser->line_length] = '\0';
    return AGNC_STATUS_OK;
}

static agnc_sse_tool_call_t *agnc_sse_get_tool_call(agnc_sse_parser_t *parser, size_t index)
{
    agnc_sse_tool_call_t *slot;

    while (parser->tool_call_count <= index && parser->tool_call_count < AGNC_SSE_MAX_TOOL_CALLS) {
        parser->tool_call_count++;
    }

    if (index >= parser->tool_call_count) {
        return NULL;
    }

    slot = &parser->tool_calls[index];
    return slot;
}

static char *agnc_string_append(char *existing, const char *extra);

static char *agnc_stream_append_with_overlap(char *existing, const char *chunk)
{
    size_t old_len;
    size_t chunk_len;
    size_t max_overlap;
    size_t overlap;
    size_t index;

    if (chunk == NULL || chunk[0] == '\0') {
        return existing;
    }

    if (existing == NULL) {
        return agnc_strdup_local(chunk);
    }

    old_len = strlen(existing);
    chunk_len = strlen(chunk);
    max_overlap = old_len < chunk_len ? old_len : chunk_len;
    overlap = 0;

    for (index = 1; index <= max_overlap; index++) {
        if (memcmp(existing + old_len - index, chunk, index) == 0) {
            overlap = index;
        }
    }

    return agnc_string_append(existing, chunk + overlap);
}

static char *agnc_stream_update_accumulator(char *existing, const char *chunk)
{
    size_t old_len;
    size_t chunk_len;
    char *updated;

    if (chunk == NULL || chunk[0] == '\0') {
        return existing;
    }

    old_len = existing != NULL ? strlen(existing) : 0;
    chunk_len = strlen(chunk);

    if (existing != NULL && strcmp(existing, chunk) == 0) {
        return existing;
    }

    if (existing != NULL && chunk_len <= old_len && strncmp(existing, chunk, chunk_len) == 0) {
        return existing;
    }

    if (existing != NULL && chunk_len >= old_len && strncmp(chunk, existing, old_len) == 0) {
        updated = agnc_strdup_local(chunk);
        free(existing);
        return updated;
    }

    return agnc_stream_append_with_overlap(existing, chunk);
}

static char *agnc_tool_arguments_update(char *existing, const char *chunk)
{
    size_t old_len;
    size_t chunk_len;

    if (chunk == NULL || chunk[0] == '\0') {
        return existing;
    }

    if (existing != NULL && strcmp(existing, chunk) == 0) {
        return existing;
    }

    old_len = existing != NULL ? strlen(existing) : 0;
    chunk_len = strlen(chunk);

    if (existing != NULL && chunk_len >= old_len && strncmp(chunk, existing, old_len) == 0) {
        char *updated = agnc_strdup_local(chunk);
        free(existing);
        return updated;
    }

    if (existing != NULL && chunk_len <= old_len && memcmp(existing + old_len - chunk_len, chunk, chunk_len) == 0) {
        return existing;
    }

    return agnc_string_append(existing, chunk);
}

static agnc_status_t agnc_stream_remember_chunk(char **slot, const char *text)
{
    char *copy;
    size_t new_len;
    size_t old_len;

    if (text == NULL || text[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    new_len = strlen(text);
    old_len = *slot != NULL ? strlen(*slot) : 0;

    if (new_len < old_len) {
        return AGNC_STATUS_OK;
    }

    copy = agnc_strdup_local(text);
    if (copy == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    free(*slot);
    *slot = copy;
    return AGNC_STATUS_OK;
}

static char *agnc_extract_path_from_jsonish(const char *text)
{
    const char *cursor;
    const char *end;
    size_t length;
    char *path;
    char *best;

    if (text == NULL) {
        return NULL;
    }

    best = NULL;
    cursor = text;
    while ((cursor = strstr(cursor, "\"path\"")) != NULL) {
        cursor += 6;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ':') {
            cursor++;
        }
        if (*cursor != '"') {
            continue;
        }
        cursor++;
        end = cursor;
        while (*end != '\0' && *end != '"') {
            end++;
        }
        if (*end != '"') {
            continue;
        }

        length = (size_t)(end - cursor);
        path = (char *)malloc(length + 1);
        if (path == NULL) {
            free(best);
            return NULL;
        }
        memcpy(path, cursor, length);
        path[length] = '\0';

        if (path[0] == '\0' || strchr(path, '{') != NULL || strchr(path, '\n') != NULL) {
            free(path);
            continue;
        }

        free(best);
        best = path;
        cursor = end + 1;
    }

    return best;
}

char *agnc_sse_tool_arguments_finalize(char *raw)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *path_value;
    char *path;
    char *clean;
    size_t path_len;

    if (raw == NULL) {
        return NULL;
    }

    doc = yyjson_read(raw, strlen(raw), 0);
    if (doc != NULL) {
        root = yyjson_doc_get_root(doc);
        path_value = yyjson_obj_get(root, "path");
        if (path_value != NULL && yyjson_is_str(path_value) && yyjson_get_str(path_value)[0] != '\0') {
            yyjson_doc_free(doc);
            return raw;
        }
        yyjson_doc_free(doc);
    }

    path = agnc_extract_path_from_jsonish(raw);
    if (path == NULL) {
        return raw;
    }

    path_len = strlen(path);
    clean = (char *)malloc(path_len + 16);
    if (clean == NULL) {
        free(path);
        return raw;
    }

    snprintf(clean, path_len + 16, "{\"path\":\"%s\"}", path);
    free(path);
    free(raw);
    return clean;
}

void agnc_sse_parser_finalize_turn(agnc_sse_parser_t *parser)
{
    if (parser->has_tool_calls && parser->tool_call_count > 0) {
        return;
    }

    /* Reasoning tidak ke stdout (hindari CoT bocor); verbose → stderr. */
    if (parser->last_reasoning_chunk != NULL && parser->last_reasoning_chunk[0] != '\0' && parser->verbose) {
        fprintf(stderr, "agnc: reasoning: %s\n", parser->last_reasoning_chunk);
    }

    if (parser->last_content_chunk == NULL || parser->last_content_chunk[0] == '\0') {
        return;
    }

    if (parser->verbose) {
        fprintf(stderr, "agnc: finalize content_len=%zu\n", strlen(parser->last_content_chunk));
    }

    /* Cetak markdown dilakukan di query.c via agnc_console_print_assistant_body(). */
    parser->printed_any = 1;
}

static agnc_status_t agnc_sse_apply_tool_calls(agnc_sse_parser_t *parser, yyjson_val *tool_calls)
{
    size_t index;
    size_t count;
    yyjson_val *value;
    yyjson_val *function;

    if (tool_calls == NULL || !yyjson_is_arr(tool_calls)) {
        return AGNC_STATUS_OK;
    }

    parser->has_tool_calls = 1;
    count = yyjson_arr_size(tool_calls);
    for (index = 0; index < count; index++) {
        agnc_sse_tool_call_t *slot;
        yyjson_val *item = yyjson_arr_get(tool_calls, index);
        yyjson_val *idx_val = yyjson_obj_get(item, "index");
        size_t slot_index = idx_val != NULL && yyjson_is_int(idx_val) ? (size_t)yyjson_get_int(idx_val) : index;

        slot = agnc_sse_get_tool_call(parser, slot_index);
        if (slot == NULL) {
            return AGNC_STATUS_PROVIDER_ERROR;
        }

        value = yyjson_obj_get(item, "id");
        if (value != NULL && yyjson_is_str(value)) {
            free(slot->id);
            slot->id = agnc_strdup_local(yyjson_get_str(value));
        }

        function = yyjson_obj_get(item, "function");
        if (function != NULL) {
            value = yyjson_obj_get(function, "name");
            if (value != NULL && yyjson_is_str(value)) {
                free(slot->name);
                slot->name = agnc_strdup_local(yyjson_get_str(value));
            }

            value = yyjson_obj_get(function, "arguments");
            if (value != NULL && yyjson_is_str(value)) {
                slot->arguments = agnc_tool_arguments_update(slot->arguments, yyjson_get_str(value));
                if (yyjson_get_str(value)[0] != '\0' && slot->arguments == NULL) {
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }
            }
        }
    }

    return AGNC_STATUS_OK;
}

static void agnc_sse_set_error(agnc_sse_parser_t *parser, const char *message)
{
    free(parser->last_error);
    parser->last_error = message != NULL ? agnc_strdup_local(message) : NULL;
}

static agnc_status_t agnc_sse_apply_choice_error(agnc_sse_parser_t *parser, yyjson_val *choice)
{
    yyjson_val *error_obj;
    yyjson_val *message;
    const char *text;

    error_obj = yyjson_obj_get(choice, "error");
    if (error_obj == NULL || !yyjson_is_obj(error_obj)) {
        return AGNC_STATUS_OK;
    }

    message = yyjson_obj_get(error_obj, "message");
    text = message != NULL && yyjson_is_str(message) ? yyjson_get_str(message) : "provider stream error";
    agnc_sse_set_error(parser, text);
    return AGNC_STATUS_PROVIDER_ERROR;
}

static agnc_status_t agnc_sse_accumulate_reasoning(agnc_sse_parser_t *parser, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    parser->last_reasoning_chunk = agnc_stream_update_accumulator(parser->last_reasoning_chunk, text);
    if (parser->last_reasoning_chunk == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_sse_accumulate_content(agnc_sse_parser_t *parser, const char *text)
{
    return agnc_stream_remember_chunk(&parser->last_content_chunk, text);
}

static agnc_status_t agnc_sse_apply_message(agnc_sse_parser_t *parser, yyjson_val *message, int stream_mode)
{
    yyjson_val *content;
    yyjson_val *reasoning;
    yyjson_val *tool_calls;
    agnc_status_t status;

    if (message == NULL || !yyjson_is_obj(message)) {
        return AGNC_STATUS_OK;
    }

    content = yyjson_obj_get(message, "content");
    if (content != NULL && yyjson_is_str(content)) {
        status = agnc_sse_accumulate_content(parser, yyjson_get_str(content));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    reasoning = yyjson_obj_get(message, "reasoning");
    if (reasoning != NULL && yyjson_is_str(reasoning)) {
        status = agnc_sse_accumulate_reasoning(parser, yyjson_get_str(reasoning));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    reasoning = yyjson_obj_get(message, "reasoning_content");
    if (reasoning != NULL && yyjson_is_str(reasoning)) {
        status = agnc_sse_accumulate_reasoning(parser, yyjson_get_str(reasoning));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    if (stream_mode && !parser->has_tool_calls) {
        tool_calls = yyjson_obj_get(message, "tool_calls");
        return agnc_sse_apply_tool_calls(parser, tool_calls);
    }

    tool_calls = yyjson_obj_get(message, "tool_calls");
    return agnc_sse_apply_tool_calls(parser, tool_calls);
}

static agnc_status_t agnc_sse_apply_delta(agnc_sse_parser_t *parser, yyjson_val *delta)
{
    yyjson_val *content;
    yyjson_val *reasoning;
    yyjson_val *tool_calls;
    agnc_status_t status;
    const char *content_text = NULL;
    const char *reasoning_text = NULL;

    if (delta == NULL || !yyjson_is_obj(delta)) {
        return AGNC_STATUS_OK;
    }

    content = yyjson_obj_get(delta, "content");
    if (content != NULL && yyjson_is_str(content)) {
        content_text = yyjson_get_str(content);
    }

    reasoning = yyjson_obj_get(delta, "reasoning");
    if (reasoning == NULL || !yyjson_is_str(reasoning)) {
        reasoning = yyjson_obj_get(delta, "reasoning_content");
    }
    if (reasoning != NULL && yyjson_is_str(reasoning)) {
        reasoning_text = yyjson_get_str(reasoning);
    }

    if (content_text != NULL && content_text[0] != '\0') {
        status = agnc_sse_accumulate_content(parser, content_text);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    if (reasoning_text != NULL && reasoning_text[0] != '\0') {
        status = agnc_sse_accumulate_reasoning(parser, reasoning_text);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    tool_calls = yyjson_obj_get(delta, "tool_calls");
    return agnc_sse_apply_tool_calls(parser, tool_calls);
}

static char *agnc_string_append(char *existing, const char *extra)
{
    size_t old_len;
    size_t extra_len;
    char *result;

    if (extra == NULL || extra[0] == '\0') {
        return existing;
    }

    old_len = existing != NULL ? strlen(existing) : 0;
    extra_len = strlen(extra);
    result = (char *)realloc(existing, old_len + extra_len + 1);
    if (result == NULL) {
        free(existing);
        return NULL;
    }

    if (old_len == 0) {
        result[0] = '\0';
    }

    memcpy(result + old_len, extra, extra_len + 1);
    return result;
}

static agnc_status_t agnc_sse_handle_event(agnc_sse_parser_t *parser, const char *payload)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *choices;
    yyjson_val *choice;
    yyjson_val *delta;
    yyjson_val *message;
    yyjson_val *error_obj;
    yyjson_val *error_message;
    agnc_status_t status;

    if (payload == NULL || payload[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    if (strcmp(payload, "[DONE]") == 0) {
        return AGNC_STATUS_OK;
    }

    doc = yyjson_read(payload, strlen(payload), 0);
    if (doc == NULL) {
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    error_obj = yyjson_obj_get(root, "error");
    if (error_obj != NULL && yyjson_is_obj(error_obj)) {
        error_message = yyjson_obj_get(error_obj, "message");
        agnc_sse_set_error(
            parser,
            error_message != NULL && yyjson_is_str(error_message) ? yyjson_get_str(error_message) : "provider error");
        yyjson_doc_free(doc);
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    choices = yyjson_obj_get(root, "choices");
    if (choices != NULL && yyjson_is_arr(choices) && yyjson_arr_size(choices) > 0) {
        choice = yyjson_arr_get(choices, 0);

        status = agnc_sse_apply_choice_error(parser, choice);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }

        delta = yyjson_obj_get(choice, "delta");
        status = agnc_sse_apply_delta(parser, delta);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }

        message = yyjson_obj_get(choice, "message");
        status = agnc_sse_apply_message(parser, message, parser->stream_mode);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }
    }

    yyjson_doc_free(doc);
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_sse_process_line(agnc_sse_parser_t *parser, char *line)
{
    if (line == NULL || line[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    if (strncmp(line, "data:", 5) == 0) {
        const char *payload = line + 5;
        while (*payload == ' ') {
            payload++;
        }
        return agnc_sse_handle_event(parser, payload);
    }

    if (line[0] == '{') {
        return agnc_sse_handle_event(parser, line);
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_sse_parser_feed(agnc_sse_parser_t *parser, const char *chunk, size_t length)
{
    size_t index = 0;
    agnc_status_t status;

    status = agnc_sse_append_line_buffer(parser, chunk, length);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    while (index < parser->line_length) {
        size_t line_start = index;
        size_t line_end = index;

        while (line_end < parser->line_length && parser->line_buffer[line_end] != '\n') {
            line_end++;
        }

        if (line_end < parser->line_length) {
            char saved = parser->line_buffer[line_end];
            parser->line_buffer[line_end] = '\0';
            status = agnc_sse_process_line(parser, parser->line_buffer + line_start);
            parser->line_buffer[line_end] = saved;
            if (status != AGNC_STATUS_OK) {
                return status;
            }

            index = line_end + 1;
        } else {
            if (line_start > 0) {
                size_t remaining = parser->line_length - line_start;
                memmove(parser->line_buffer, parser->line_buffer + line_start, remaining);
                parser->line_length = remaining;
                parser->line_buffer[parser->line_length] = '\0';
            }
            break;
        }
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_sse_parser_flush(agnc_sse_parser_t *parser)
{
    agnc_status_t status = AGNC_STATUS_OK;

    if (parser->line_length > 0) {
        parser->line_buffer[parser->line_length] = '\0';
        status = agnc_sse_process_line(parser, parser->line_buffer);
        parser->line_length = 0;
    }
    return status;
}
