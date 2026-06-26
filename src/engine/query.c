/*
 * query.c
 *
 * Agent loop Fase 1: streaming OpenAI-compatible + eksekusi read_file.
 */

#include "agnc/query.h"
#include "agnc/net/http.h"
#include "agnc/tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <yyjson.h>

#define AGNC_MAX_TOOL_CALLS 8
#define AGNC_MAX_MESSAGES 64
#define AGNC_LINE_BUFFER_INITIAL 4096

/* Satu pesan dalam riwayat chat OpenAI-compatible (user/assistant/tool). */
typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    char *tool_name;
    char *tool_arguments;
} agnc_chat_message_t;

/* Akumulator tool call dari chunk SSE; arguments dirakit incremental. */
typedef struct {
    char *id;
    char *name;
    char *arguments;
} agnc_tool_call_acc_t;

/*
 * Parser respons provider: buffer baris SSE/JSON, teks jawaban, dan tool calls.
 * last_output_chunk menyimpan snapshot terpanjang (beberapa model kirim teks kumulatif).
 */
typedef struct {
    char *line_buffer;
    size_t line_length;
    size_t line_capacity;
    char *last_output_chunk;
    char *last_error;
    agnc_tool_call_acc_t tool_calls[AGNC_MAX_TOOL_CALLS];
    size_t tool_call_count;
    int has_tool_calls;
    int printed_any;
    const agnc_config_t *config;
} agnc_stream_parser_t;

typedef struct {
    agnc_chat_message_t items[AGNC_MAX_MESSAGES];
    size_t count;
} agnc_message_list_t;

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static void agnc_message_list_init(agnc_message_list_t *list)
{
    memset(list, 0, sizeof(*list));
}

static void agnc_message_list_clear(agnc_message_list_t *list)
{
    size_t index;

    for (index = 0; index < list->count; index++) {
        free(list->items[index].role);
        free(list->items[index].content);
        free(list->items[index].tool_call_id);
        free(list->items[index].tool_name);
        free(list->items[index].tool_arguments);
    }

    list->count = 0;
}

static agnc_status_t agnc_message_list_push(
    agnc_message_list_t *list,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments)
{
    agnc_chat_message_t *message;

    if (list->count >= AGNC_MAX_MESSAGES) {
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    message = &list->items[list->count++];
    message->role = agnc_strdup_local(role);
    message->content = content != NULL ? agnc_strdup_local(content) : NULL;
    message->tool_call_id = tool_call_id != NULL ? agnc_strdup_local(tool_call_id) : NULL;
    message->tool_name = tool_name != NULL ? agnc_strdup_local(tool_name) : NULL;
    message->tool_arguments = tool_arguments != NULL ? agnc_strdup_local(tool_arguments) : NULL;

    if (message->role == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static void agnc_stream_parser_init(agnc_stream_parser_t *parser, const agnc_config_t *config)
{
    memset(parser, 0, sizeof(*parser));
    parser->config = config;
}

static void agnc_stream_parser_free(agnc_stream_parser_t *parser)
{
    size_t index;

    free(parser->line_buffer);
    free(parser->last_output_chunk);
    free(parser->last_error);
    parser->line_buffer = NULL;
    parser->last_output_chunk = NULL;
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

static agnc_status_t agnc_stream_parser_append_line_buffer(agnc_stream_parser_t *parser, const char *data, size_t length)
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

static agnc_tool_call_acc_t *agnc_stream_parser_get_tool_call(agnc_stream_parser_t *parser, size_t index)
{
    agnc_tool_call_acc_t *slot;

    while (parser->tool_call_count <= index && parser->tool_call_count < AGNC_MAX_TOOL_CALLS) {
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

static char *agnc_stream_content_update(char *existing, const char *chunk)
{
    size_t old_len;
    size_t chunk_len;
    char *updated;

    if (chunk == NULL || chunk[0] == '\0') {
        return existing;
    }

    chunk_len = strlen(chunk);
    old_len = existing != NULL ? strlen(existing) : 0;

    if (existing != NULL && strcmp(existing, chunk) == 0) {
        return existing;
    }

    /*
     * gpt-oss (OpenRouter) mengirim delta.content sebagai teks kumulatif penuh
     * yang memanjang tiap chunk. Simpan snapshot terpanjang — jangan append.
     */
    if (chunk_len >= old_len) {
        updated = agnc_strdup_local(chunk);
        free(existing);
        return updated;
    }

    return existing;
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

    /* Chunk sudah ada sebagai prefix accumulator — abaikan (gpt-oss kadang re-send). */
    if (existing != NULL && chunk_len <= old_len && strncmp(existing, chunk, chunk_len) == 0) {
        return existing;
    }

    /*
     * Beberapa model OpenRouter mengirim teks kumulatif (full-so-far), bukan delta.
     * Jika chunk baru memperpanjang prefix lama, ganti accumulator — jangan append penuh.
     */
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

    /* Kumulatif: chunk baru adalah versi lengkap yang memperpanjang string lama. */
    if (existing != NULL && chunk_len >= old_len && strncmp(chunk, existing, old_len) == 0) {
        char *updated = agnc_strdup_local(chunk);
        free(existing);
        return updated;
    }

    /* Incremental duplikat: chunk sudah ada di akhir string. */
    if (existing != NULL && chunk_len <= old_len && memcmp(existing + old_len - chunk_len, chunk, chunk_len) == 0) {
        return existing;
    }

    return agnc_string_append(existing, chunk);
}

static agnc_status_t agnc_stream_accumulate_text(char **accumulator, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    *accumulator = agnc_stream_update_accumulator(*accumulator, text);
    if (*accumulator == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
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

    /* Simpan snapshot kumulatif terpanjang (chunk akhir kadang hanya "."). */
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

static agnc_status_t agnc_stream_accumulate_output(agnc_stream_parser_t *parser, const char *text)
{
    return agnc_stream_remember_chunk(&parser->last_output_chunk, text);
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

        /* Abaikan fragmen JSON rusak; ambil kandidat path file yang valid. */
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

/*
 * Beberapa model (mis. gpt-oss) mengirim arguments tool sebagai fragmen stream.
 * Jika JSON belum valid, coba ekstrak "path" lalu bangun ulang objek minimal.
 */
static char *agnc_tool_arguments_finalize(char *raw)
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

/* Cetak jawaban teks sekali di akhir turn (--print non-stream). */
static void agnc_stream_parser_finalize_turn(agnc_stream_parser_t *parser)
{
    if (parser->has_tool_calls && parser->tool_call_count > 0) {
        return;
    }

    if (parser->last_output_chunk == NULL || parser->last_output_chunk[0] == '\0') {
        return;
    }

    if (parser->config != NULL && parser->config->verbose) {
        fprintf(stderr, "agnc: finalize output_len=%zu\n", strlen(parser->last_output_chunk));
    }

    fputs(parser->last_output_chunk, stdout);
    fflush(stdout);
    parser->printed_any = 1;
}

static agnc_status_t agnc_stream_parser_apply_tool_calls(agnc_stream_parser_t *parser, yyjson_val *tool_calls)
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
        agnc_tool_call_acc_t *slot;
        yyjson_val *item = yyjson_arr_get(tool_calls, index);
        yyjson_val *idx_val = yyjson_obj_get(item, "index");
        size_t slot_index = idx_val != NULL && yyjson_is_int(idx_val) ? (size_t)yyjson_get_int(idx_val) : index;

        slot = agnc_stream_parser_get_tool_call(parser, slot_index);
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

static void agnc_stream_parser_set_error(agnc_stream_parser_t *parser, const char *message)
{
    free(parser->last_error);
    parser->last_error = message != NULL ? agnc_strdup_local(message) : NULL;
}

static agnc_status_t agnc_stream_parser_apply_choice_error(agnc_stream_parser_t *parser, yyjson_val *choice)
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
    agnc_stream_parser_set_error(parser, text);
    return AGNC_STATUS_PROVIDER_ERROR;
}

static agnc_status_t agnc_stream_parser_apply_message(agnc_stream_parser_t *parser, yyjson_val *message, int stream_mode)
{
    yyjson_val *content;
    yyjson_val *reasoning;
    yyjson_val *tool_calls;
    agnc_status_t status;

    if (message == NULL || !yyjson_is_obj(message)) {
        return AGNC_STATUS_OK;
    }

    if (stream_mode) {
        content = yyjson_obj_get(message, "content");
        if (content != NULL && yyjson_is_str(content)) {
            status = agnc_stream_accumulate_output(parser, yyjson_get_str(content));
            if (status != AGNC_STATUS_OK) {
                return status;
            }
        }

        reasoning = yyjson_obj_get(message, "reasoning");
        if (reasoning != NULL && yyjson_is_str(reasoning)) {
            status = agnc_stream_accumulate_output(parser, yyjson_get_str(reasoning));
            if (status != AGNC_STATUS_OK) {
                return status;
            }
        }

        reasoning = yyjson_obj_get(message, "reasoning_content");
        if (reasoning != NULL && yyjson_is_str(reasoning)) {
            status = agnc_stream_accumulate_output(parser, yyjson_get_str(reasoning));
            if (status != AGNC_STATUS_OK) {
                return status;
            }
        }

        if (!parser->has_tool_calls) {
            tool_calls = yyjson_obj_get(message, "tool_calls");
            return agnc_stream_parser_apply_tool_calls(parser, tool_calls);
        }
        return AGNC_STATUS_OK;
    }

    content = yyjson_obj_get(message, "content");
    if (content != NULL && yyjson_is_str(content)) {
        status = agnc_stream_accumulate_output(parser, yyjson_get_str(content));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    reasoning = yyjson_obj_get(message, "reasoning");
    if (reasoning != NULL && yyjson_is_str(reasoning)) {
        status = agnc_stream_accumulate_output(parser, yyjson_get_str(reasoning));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    reasoning = yyjson_obj_get(message, "reasoning_content");
    if (reasoning != NULL && yyjson_is_str(reasoning)) {
        status = agnc_stream_accumulate_output(parser, yyjson_get_str(reasoning));
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    tool_calls = yyjson_obj_get(message, "tool_calls");
    return agnc_stream_parser_apply_tool_calls(parser, tool_calls);
}

static agnc_status_t agnc_stream_parser_apply_delta(agnc_stream_parser_t *parser, yyjson_val *delta)
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

    /*
     * gpt-oss mengirim snapshot kumulatif per chunk; simpan yang terpanjang.
     * Utamakan content; reasoning hanya fallback jika content kosong.
     */
    if (content_text != NULL && content_text[0] != '\0') {
        status = agnc_stream_accumulate_output(parser, content_text);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    if (reasoning_text != NULL && reasoning_text[0] != '\0') {
        status = agnc_stream_accumulate_output(parser, reasoning_text);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    tool_calls = yyjson_obj_get(delta, "tool_calls");
    return agnc_stream_parser_apply_tool_calls(parser, tool_calls);
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

static agnc_status_t agnc_stream_parser_handle_event(agnc_stream_parser_t *parser, const char *payload)
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
    int stream_mode;

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
        agnc_stream_parser_set_error(
            parser,
            error_message != NULL && yyjson_is_str(error_message) ? yyjson_get_str(error_message) : "provider error");
        yyjson_doc_free(doc);
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    choices = yyjson_obj_get(root, "choices");
    stream_mode = parser->config != NULL && parser->config->stream;
    if (choices != NULL && yyjson_is_arr(choices) && yyjson_arr_size(choices) > 0) {
        choice = yyjson_arr_get(choices, 0);

        status = agnc_stream_parser_apply_choice_error(parser, choice);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }

        /* Mode stream: delta per chunk; non-stream: message lengkap di akhir body. */
        delta = yyjson_obj_get(choice, "delta");
        status = agnc_stream_parser_apply_delta(parser, delta);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }

        message = yyjson_obj_get(choice, "message");
        status = agnc_stream_parser_apply_message(parser, message, stream_mode);
        if (status != AGNC_STATUS_OK) {
            yyjson_doc_free(doc);
            return status;
        }
    }

    yyjson_doc_free(doc);
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_stream_parser_process_line(agnc_stream_parser_t *parser, char *line)
{
    if (line == NULL || line[0] == '\0') {
        return AGNC_STATUS_OK;
    }

    if (strncmp(line, "data:", 5) == 0) {
        const char *payload = line + 5;
        while (*payload == ' ') {
            payload++;
        }
        return agnc_stream_parser_handle_event(parser, payload);
    }

    /* Respons non-stream: satu objek JSON utuh, bukan baris SSE. */
    if (line[0] == '{') {
        return agnc_stream_parser_handle_event(parser, line);
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_stream_parser_feed(agnc_stream_parser_t *parser, const char *chunk, size_t length)
{
    size_t index = 0;
    agnc_status_t status;

    status = agnc_stream_parser_append_line_buffer(parser, chunk, length);
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
            status = agnc_stream_parser_process_line(parser, parser->line_buffer + line_start);
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

static agnc_status_t agnc_stream_callback(void *user_data, const char *chunk, size_t length)
{
    return agnc_stream_parser_feed((agnc_stream_parser_t *)user_data, chunk, length);
}

static char *agnc_build_chat_url(const char *base_url)
{
    size_t length = strlen(base_url);
    char *url;

    while (length > 0 && base_url[length - 1] == '/') {
        length--;
    }

    url = (char *)malloc(length + 32);
    if (url == NULL) {
        return NULL;
    }

    snprintf(url, length + 32, "%.*s/chat/completions", (int)length, base_url);
    return url;
}

static char *agnc_build_request_json(
    const agnc_config_t *config,
    const agnc_message_list_t *messages)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *messages_arr;
    yyjson_mut_val *tools_arr;
    yyjson_mut_val *tool_obj;
    yyjson_mut_val *function_obj;
    yyjson_mut_val *parameters_obj;
    yyjson_mut_val *properties_obj;
    yyjson_mut_val *path_obj;
    yyjson_mut_val *required_arr;
    char *result;
    size_t index;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "model", config->model);
    yyjson_mut_obj_add_bool(doc, root, "stream", config->stream ? true : false);
    /* Model tanpa native tool use (mis. LFM free) mengembalikan HTTP 404 jika tools dikirim. */
    yyjson_mut_obj_add_str(doc, root, "tool_choice", "auto");

    messages_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "messages", messages_arr);

    for (index = 0; index < messages->count; index++) {
        const agnc_chat_message_t *message = &messages->items[index];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, entry, "role", message->role);

        if (strcmp(message->role, "tool") == 0) {
            yyjson_mut_obj_add_str(doc, entry, "tool_call_id", message->tool_call_id);
            yyjson_mut_obj_add_str(doc, entry, "content", message->content != NULL ? message->content : "");
        } else if (message->tool_name != NULL) {
            yyjson_mut_val *tool_calls = yyjson_mut_arr(doc);
            yyjson_mut_val *tool_call = yyjson_mut_obj(doc);
            yyjson_mut_val *function = yyjson_mut_obj(doc);

            yyjson_mut_arr_append(tool_calls, tool_call);
            yyjson_mut_obj_add_val(doc, entry, "tool_calls", tool_calls);
            yyjson_mut_obj_add_str(doc, tool_call, "id", message->tool_call_id);
            yyjson_mut_obj_add_str(doc, tool_call, "type", "function");
            yyjson_mut_obj_add_val(doc, tool_call, "function", function);
            yyjson_mut_obj_add_str(doc, function, "name", message->tool_name);
            yyjson_mut_obj_add_str(doc, function, "arguments", message->tool_arguments != NULL ? message->tool_arguments : "{}");
            if (message->content != NULL) {
                yyjson_mut_obj_add_str(doc, entry, "content", message->content);
            }
        } else {
            yyjson_mut_obj_add_str(doc, entry, "content", message->content != NULL ? message->content : "");
        }

        yyjson_mut_arr_append(messages_arr, entry);
    }

    /* Fase 1: hanya read_file; schema mengikuti OpenAI function calling. */
    tools_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "tools", tools_arr);
    tool_obj = yyjson_mut_obj(doc);
    function_obj = yyjson_mut_obj(doc);
    parameters_obj = yyjson_mut_obj(doc);
    properties_obj = yyjson_mut_obj(doc);
    path_obj = yyjson_mut_obj(doc);
    required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "read_file");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Read a text file from disk and return its contents.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    yyjson_mut_obj_add_val(doc, properties_obj, "path", path_obj);
    yyjson_mut_obj_add_str(doc, path_obj, "type", "string");
    yyjson_mut_obj_add_str(doc, path_obj, "description", "Absolute or relative path to the file.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "path"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);

    result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static agnc_status_t agnc_run_provider_turn(
    const agnc_config_t *config,
    const agnc_message_list_t *messages,
    agnc_stream_parser_t *parser,
    char **error_message)
{
    char *url;
    char *auth_header;
    char *request_json;
    agnc_status_t status;
    size_t auth_length;

    url = agnc_build_chat_url(config->base_url);
    if (url == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    auth_length = strlen(config->api_key) + 32;
    auth_header = (char *)malloc(auth_length);
    if (auth_header == NULL) {
        free(url);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    snprintf(auth_header, auth_length, "Authorization: Bearer %s", config->api_key);
    request_json = agnc_build_request_json(config, messages);
    if (request_json == NULL) {
        free(auth_header);
        free(url);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (config->verbose) {
        fprintf(stderr, "agnc: POST %s\n", url);
    }

    status = agnc_http_post_stream(url, auth_header, request_json, agnc_stream_callback, parser, error_message);

    if (status == AGNC_STATUS_OK && parser->last_error != NULL) {
        if (error_message != NULL && *error_message == NULL) {
            *error_message = agnc_strdup_local(parser->last_error);
        }
        status = AGNC_STATUS_PROVIDER_ERROR;
    }

    /* Proses baris SSE terakhir yang mungkin tidak diakhiri newline. */
    if (status == AGNC_STATUS_OK && parser->line_length > 0) {
        parser->line_buffer[parser->line_length] = '\0';
        status = agnc_stream_parser_process_line(parser, parser->line_buffer);
        parser->line_length = 0;
    }

    free(request_json);
    free(auth_header);
    free(url);
    return status;
}

agnc_status_t agnc_query_print(const agnc_config_t *config, const char *prompt)
{
    agnc_message_list_t messages;
    agnc_stream_parser_t parser;
    agnc_status_t status;
    size_t iteration;
    char *error_message = NULL;
    int curl_initialized = 0;
    int any_output = 0;

    if (config == NULL || prompt == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    memset(&parser, 0, sizeof(parser));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
        curl_initialized = 1;
    }

    agnc_message_list_init(&messages);
    status = agnc_message_list_push(
        &messages,
        "system",
        "You are a helpful coding assistant. Use the read_file tool when you need file contents.",
        NULL,
        NULL,
        NULL);
    if (status != AGNC_STATUS_OK) {
        goto cleanup;
    }

    status = agnc_message_list_push(&messages, "user", prompt, NULL, NULL, NULL);
    if (status != AGNC_STATUS_OK) {
        goto cleanup;
    }

    /*
     * Loop agent: POST ke provider → tool call? → eksekusi lokal → POST lagi.
     * Batas iterasi mencegah loop tak terbatas jika model terus memanggil tool.
     */
    for (iteration = 0; iteration < (size_t)config->max_tool_iterations; iteration++) {
        agnc_stream_parser_free(&parser);
        agnc_stream_parser_init(&parser, config);

        status = agnc_run_provider_turn(config, &messages, &parser, &error_message);
        if (status != AGNC_STATUS_OK) {
            goto cleanup;
        }

        agnc_stream_parser_finalize_turn(&parser);

        if (parser.printed_any) {
            any_output = 1;
        }

        if (!parser.has_tool_calls || parser.tool_call_count == 0) {
            if (any_output) {
                fputc('\n', stdout);
            }
            status = AGNC_STATUS_OK;
            goto cleanup;
        }

        /*
         * Fase 1 spike: dukung satu tool call per putaran provider.
         * Beberapa model bisa mengirim lebih dari satu; sisanya diabaikan sementara.
         */
        if (parser.tool_call_count > 1 && config->verbose) {
            fprintf(stderr, "agnc: warning: multiple tool calls received, using the first one\n");
        }

        {
            agnc_tool_call_acc_t *tool_call = &parser.tool_calls[0];
            char *tool_result = NULL;
            char synthetic_id[48];

            if (tool_call->name == NULL) {
                status = AGNC_STATUS_PROVIDER_ERROR;
                agnc_stream_parser_set_error(&parser, "tool call missing function name");
                goto cleanup;
            }

            if (tool_call->id == NULL) {
                snprintf(synthetic_id, sizeof(synthetic_id), "call_%zu", iteration);
                tool_call->id = agnc_strdup_local(synthetic_id);
                if (tool_call->id == NULL) {
                    status = AGNC_STATUS_OUT_OF_MEMORY;
                    goto cleanup;
                }
                if (config->verbose) {
                    fprintf(stderr, "agnc: warning: tool call missing id, using %s\n", synthetic_id);
                }
            }

            tool_call->arguments = agnc_tool_arguments_finalize(tool_call->arguments);

            if (config->verbose) {
                fprintf(
                    stderr,
                    "agnc: tool call %s(%s)\n",
                    tool_call->name,
                    tool_call->arguments != NULL ? tool_call->arguments : "{}");
            }

            status = agnc_message_list_push(
                &messages,
                "assistant",
                NULL,
                tool_call->id,
                tool_call->name,
                tool_call->arguments);
            if (status != AGNC_STATUS_OK) {
                goto cleanup;
            }

            if (strcmp(tool_call->name, "read_file") == 0) {
                status = agnc_tool_read_file_execute(tool_call->arguments, &tool_result);
            } else {
                tool_result = agnc_strdup_local("error: unsupported tool");
                status = AGNC_STATUS_TOOL_FAILED;
            }

            if (tool_result == NULL && status != AGNC_STATUS_OK) {
                status = AGNC_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }

            status = agnc_message_list_push(
                &messages,
                "tool",
                tool_result != NULL ? tool_result : "error: tool failed",
                tool_call->id,
                NULL,
                NULL);
            free(tool_result);

            if (status != AGNC_STATUS_OK) {
                goto cleanup;
            }
        }

        if (parser.printed_any) {
            fputc('\n', stdout);
        }
    }

    status = AGNC_STATUS_PROVIDER_ERROR;
    fprintf(stderr, "agnc: max tool iterations reached\n");

cleanup:
    if (status == AGNC_STATUS_OK && !any_output) {
        fprintf(stderr, "agnc: warning: provider returned no visible output (enable runtime.verbose for details)\n");
    }

    if (status != AGNC_STATUS_OK) {
        if (error_message != NULL) {
            fprintf(stderr, "agnc: %s\n", error_message);
        } else if (parser.last_error != NULL) {
            fprintf(stderr, "agnc: %s\n", parser.last_error);
        }
    }

    free(error_message);

    agnc_stream_parser_free(&parser);
    agnc_message_list_clear(&messages);

    if (curl_initialized) {
        curl_global_cleanup();
    }

    return status;
}
