/*
 * sub_agent.c
 *
 * Tool sub_agent: jalankan agnc_query_run isolasi dan kembalikan jawaban akhir.
 */

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/query.h"
#include "agnc/status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_SUB_AGENT_MAX_DEPTH 1

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static const char *agnc_sub_agent_last_assistant(const agnc_conversation_t *conversation)
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

agnc_status_t agnc_tool_sub_agent_execute(
    const agnc_config_t *parent_config,
    const agnc_query_options_t *parent_options,
    int agent_depth,
    const char *arguments_json,
    char **result_text)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *prompt_val;
    yyjson_val *iter_val;
    const char *prompt;
    agnc_config_t child_config;
    agnc_conversation_t child_conversation;
    agnc_query_options_t child_options;
    agnc_status_t status;
    const char *assistant;

    if (parent_config == NULL || arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;

    if (agent_depth >= AGNC_SUB_AGENT_MAX_DEPTH) {
        *result_text = agnc_strdup_local("error: sub_agent depth limit reached");
        return AGNC_STATUS_TOOL_FAILED;
    }

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        *result_text = agnc_strdup_local("error: invalid sub_agent JSON");
        return AGNC_STATUS_TOOL_FAILED;
    }

    root = yyjson_doc_get_root(doc);
    prompt_val = yyjson_obj_get(root, "prompt");
    if (prompt_val == NULL || !yyjson_is_str(prompt_val)) {
        yyjson_doc_free(doc);
        *result_text = agnc_strdup_local("error: sub_agent requires prompt");
        return AGNC_STATUS_TOOL_FAILED;
    }

    prompt = yyjson_get_str(prompt_val);
    agnc_config_init(&child_config);
    child_config.base_url = agnc_strdup_local(parent_config->base_url);
    child_config.model = agnc_strdup_local(parent_config->model);
    child_config.api_key = agnc_strdup_local(parent_config->api_key);
    child_config.provider_id = agnc_strdup_local(parent_config->provider_id);
    child_config.gateway_id = agnc_strdup_local(parent_config->gateway_id);
    child_config.max_tool_iterations = parent_config->max_tool_iterations;
    child_config.stream = 0;
    child_config.verbose = parent_config->verbose;
    child_config.enable_tools = parent_config->enable_tools;
    child_config.tool_read_file = parent_config->tool_read_file;
    child_config.tool_shell = parent_config->tool_shell;
    child_config.tool_write_file = parent_config->tool_write_file;
    child_config.tool_edit_file = parent_config->tool_edit_file;
    child_config.tool_grep = parent_config->tool_grep;
    child_config.tool_glob = parent_config->tool_glob;
    child_config.tool_web_fetch = parent_config->tool_web_fetch;
    child_config.tool_todo_write = parent_config->tool_todo_write;
    child_config.tool_find_symbol = parent_config->tool_find_symbol;
    child_config.tool_sub_agent = 0;

    iter_val = yyjson_obj_get(root, "max_iterations");
    if (iter_val != NULL && yyjson_is_num(iter_val)) {
        long value = (long)yyjson_get_num(iter_val);
        if (value > 0 && value < child_config.max_tool_iterations) {
            child_config.max_tool_iterations = (int)value;
        }
    }

    yyjson_doc_free(doc);

    agnc_conversation_init(&child_conversation);
    memset(&child_options, 0, sizeof(child_options));
    if (parent_options != NULL) {
        child_options = *parent_options;
        child_options.chat_assistant_timestamp = 0;
        child_options.stream_live_print = 0;
        child_options.session_sqlite_path = NULL;
        child_options.session_name = NULL;
        child_options.agent_depth = agent_depth + 1;
    } else {
        child_options.agent_depth = agent_depth + 1;
    }

    status = agnc_query_run(&child_config, &child_conversation, prompt, &child_options);
    assistant = agnc_sub_agent_last_assistant(&child_conversation);

    if (status == AGNC_STATUS_OK && assistant != NULL) {
        *result_text = agnc_strdup_local(assistant);
    } else if (status != AGNC_STATUS_OK) {
        char detail[128];
        snprintf(detail, sizeof(detail), "error: sub_agent %s", agnc_status_to_string(status));
        *result_text = agnc_strdup_local(detail);
        status = AGNC_STATUS_TOOL_FAILED;
    } else {
        *result_text = agnc_strdup_local("error: sub_agent returned no assistant text");
        status = AGNC_STATUS_TOOL_FAILED;
    }

    agnc_conversation_clear(&child_conversation);
    agnc_config_free(&child_config);

    if (*result_text == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return status == AGNC_STATUS_OK ? AGNC_STATUS_OK : AGNC_STATUS_TOOL_FAILED;
}
