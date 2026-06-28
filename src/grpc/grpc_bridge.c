/*
 * grpc_bridge.c
 *
 * Jalankan satu turn agent untuk klien gRPC (suppress stdout).
 */

#include "agnc/grpc_bridge.h"

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/path.h"
#include "agnc/permissions.h"
#include "agnc/query.h"
#include "agnc/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const agnc_grpc_query_request_t *request;
    int delta_sent;
} agnc_grpc_stream_track_t;

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static int agnc_grpc_permission_bridge(const char *kind, const char *detail, void *ctx)
{
    const agnc_grpc_query_request_t *request = (const agnc_grpc_query_request_t *)ctx;

    if (request == NULL || request->permission_ask == NULL) {
        return 0;
    }

    return request->permission_ask(kind, detail, request->permission_ask_ctx) ? 1 : 0;
}

static void agnc_grpc_stream_delta_track(const char *text, size_t length, void *ctx)
{
    agnc_grpc_stream_track_t *track = (agnc_grpc_stream_track_t *)ctx;
    const agnc_grpc_query_request_t *request;

    if (track == NULL || track->request == NULL || track->request->stream_delta == NULL || text == NULL ||
        length == 0) {
        return;
    }

    track->delta_sent = 1;
    request = track->request;
    request->stream_delta(text, length, request->stream_delta_ctx);
}

static const char *agnc_grpc_last_assistant(const agnc_conversation_t *conversation)
{
    size_t index;

    if (conversation == NULL) {
        return NULL;
    }

    for (index = conversation->count; index > 0; index--) {
        const agnc_conversation_message_t *message = agnc_conversation_at(conversation, index - 1);

        if (message != NULL && message->role != NULL && strcmp(message->role, "assistant") == 0 &&
            message->content != NULL && message->content[0] != '\0' && message->tool_name == NULL) {
            return message->content;
        }
    }

    return NULL;
}

void agnc_grpc_query_result_init(agnc_grpc_query_result_t *result)
{
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->usage_prompt_tokens = -1;
    result->usage_completion_tokens = -1;
    result->usage_total_tokens = -1;
}

void agnc_grpc_query_result_free(agnc_grpc_query_result_t *result)
{
    if (result == NULL) {
        return;
    }

    free(result->assistant_text);
    free(result->error_message);
    result->assistant_text = NULL;
    result->error_message = NULL;
}

const char *agnc_grpc_status_label(agnc_status_t status)
{
    switch (status) {
    case AGNC_STATUS_OK:
        return "ok";
    case AGNC_STATUS_CANCELLED:
        return "cancelled";
    default:
        return agnc_status_to_string(status);
    }
}

agnc_status_t agnc_grpc_run_query(
    const agnc_grpc_query_request_t *request,
    agnc_grpc_query_result_t *result)
{
    agnc_config_t config;
    agnc_conversation_t conversation;
    agnc_query_options_t options;
    agnc_grpc_stream_track_t stream_track;
    char *session_path = NULL;
    char *query_error = NULL;
    const char *assistant;
    agnc_status_t status;

    if (request == NULL || result == NULL || request->prompt == NULL || request->prompt[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_grpc_query_result_init(result);

    agnc_config_init(&config);
    status = agnc_config_load(NULL, &config);
    if (status != AGNC_STATUS_OK) {
        result->status = status;
        result->error_message = agnc_strdup_local("failed to load ~/.agnc.json");
        agnc_config_free(&config);
        return status;
    }

    config.stream = request->stream_delta != NULL ? 1 : 0;

    if (!request->enable_tools) {
        config.enable_tools = 0;
        config.tool_read_file = 0;
        config.tool_shell = 0;
        config.tool_write_file = 0;
        config.tool_edit_file = 0;
        config.tool_grep = 0;
        config.tool_glob = 0;
        config.tool_web_fetch = 0;
        config.tool_todo_write = 0;
        config.tool_find_symbol = 0;
        config.tool_sub_agent = 0;
    }

    if (request->auto_approve) {
        config.ask_shell_permission = 0;
        config.ask_write_permission = 0;
        config.ask_mcp_permission = 0;
        config.ask_web_fetch_permission = 0;
    }

    agnc_conversation_init(&conversation);

    memset(&options, 0, sizeof(options));
    options.cancel_flag = request->cancel_flag;
    options.auto_approve = request->auto_approve ? 1 : 0;
    options.suppress_chat_output = 1;
    options.error_message_out = &query_error;
    options.usage_prompt_tokens = &result->usage_prompt_tokens;
    options.usage_completion_tokens = &result->usage_completion_tokens;
    options.usage_total_tokens = &result->usage_total_tokens;

    memset(&stream_track, 0, sizeof(stream_track));
    stream_track.request = request;
    if (request->stream_delta != NULL) {
        options.stream_delta_fn = agnc_grpc_stream_delta_track;
        options.stream_delta_ctx = &stream_track;
    }

    if (request->session_name != NULL && request->session_name[0] != '\0') {
        status = agnc_session_path_for_name(request->session_name, &session_path);
        if (status != AGNC_STATUS_OK) {
            result->status = status;
            result->error_message = agnc_strdup_local("invalid session name");
            agnc_conversation_clear(&conversation);
            agnc_config_free(&config);
            return status;
        }
        options.session_name = request->session_name;
        options.session_sqlite_path = session_path;
    }

    if (!request->auto_approve && request->permission_ask != NULL) {
        agnc_permission_set_background_ask(agnc_grpc_permission_bridge, (void *)request);
    }

    status = agnc_query_run(&config, &conversation, request->prompt, &options);

    if (!request->auto_approve && request->permission_ask != NULL) {
        agnc_permission_clear_background_ask();
    }

    result->status = status;

    if (status == AGNC_STATUS_OK) {
        assistant = agnc_grpc_last_assistant(&conversation);
        if (assistant != NULL) {
            result->assistant_text = agnc_strdup_local(assistant);
            if (result->assistant_text == NULL) {
                status = AGNC_STATUS_OUT_OF_MEMORY;
                result->status = status;
            } else if (request->stream_delta != NULL && assistant[0] != '\0' && !stream_track.delta_sent) {
                request->stream_delta(assistant, strlen(assistant), request->stream_delta_ctx);
            }
        }
    } else if (query_error != NULL) {
        result->error_message = query_error;
        query_error = NULL;
    } else {
        result->error_message = agnc_strdup_local(agnc_status_to_string(status));
    }

    free(query_error);
    free(session_path);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    return status;
}
