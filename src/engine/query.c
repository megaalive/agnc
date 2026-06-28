/*
 * query.c
 *
 * Agent loop: streaming OpenAI-compatible + eksekusi tool Fase 1–2.
 */

#include "agnc/query.h"
#include "agnc/console.h"
#include "agnc/conversation.h"
#include "agnc/net/http.h"
#include "agnc/net/sse.h"
#include "agnc/path.h"
#include "agnc/hooks.h"
#include "agnc/permissions.h"
#include "agnc/provider.h"
#include "agnc/opencode.h"
#include "agnc/anthropic.h"
#include "agnc/cost.h"
#include "agnc/session.h"
#include "agnc/skills.h"
#include "agnc/status.h"
#include "agnc/tool.h"
#include "agnc/tool_cache.h"
#include "agnc/tool_path.h"
#include "agnc/mcp/registry.h"
#include "agnc/mcp/tools.h"
#include "agnc/mcp/session.h"
#include "agnc/mcp/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <yyjson.h>

#define AGNC_TOOL_RESULT_MAX 6000
#define AGNC_MCP_TIMEOUT_MS 30000

static void agnc_query_accumulate_usage(const agnc_sse_parser_t *parser, const agnc_query_options_t *options)
{
    if (parser == NULL || options == NULL || !agnc_sse_parser_has_usage(parser)) {
        return;
    }

    if (options->usage_prompt_tokens != NULL) {
        long value = agnc_sse_parser_get_prompt_tokens(parser);

        if (value >= 0) {
            if (*options->usage_prompt_tokens < 0) {
                *options->usage_prompt_tokens = 0;
            }
            *options->usage_prompt_tokens += value;
        }
    }

    if (options->usage_completion_tokens != NULL) {
        long value = agnc_sse_parser_get_completion_tokens(parser);

        if (value >= 0) {
            if (*options->usage_completion_tokens < 0) {
                *options->usage_completion_tokens = 0;
            }
            *options->usage_completion_tokens += value;
        }
    }

    if (options->usage_total_tokens != NULL) {
        long value = agnc_sse_parser_get_total_tokens(parser);

        if (value >= 0) {
            if (*options->usage_total_tokens < 0) {
                *options->usage_total_tokens = 0;
            }
            *options->usage_total_tokens += value;
        }
    }
}

static void agnc_query_accumulate_cost(
    const agnc_config_t *config,
    const agnc_sse_parser_t *parser,
    const agnc_query_options_t *options)
{
    long prompt_tokens;
    long completion_tokens;
    double cost_usd;

    if (config == NULL || parser == NULL || options == NULL || options->session_sqlite_path == NULL) {
        return;
    }

    if (!agnc_sse_parser_has_usage(parser)) {
        return;
    }

    prompt_tokens = agnc_sse_parser_get_prompt_tokens(parser);
    completion_tokens = agnc_sse_parser_get_completion_tokens(parser);
    cost_usd = agnc_cost_estimate_turn_usd(
        config->model,
        config->provider_id,
        prompt_tokens,
        completion_tokens);
    if (cost_usd > 0.0) {
        (void)agnc_session_cost_accumulate(options->session_sqlite_path, cost_usd);
    }
}

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static void agnc_query_truncate_tool_result(char **tool_result)
{
    size_t len;
    char *resized;
    static const char suffix[] = "\n...(output truncated)";

    if (tool_result == NULL || *tool_result == NULL) {
        return;
    }

    len = strlen(*tool_result);
    if (len <= AGNC_TOOL_RESULT_MAX) {
        return;
    }

    resized = (char *)realloc(*tool_result, AGNC_TOOL_RESULT_MAX + sizeof(suffix));
    if (resized == NULL) {
        (*tool_result)[AGNC_TOOL_RESULT_MAX] = '\0';
        return;
    }

    *tool_result = resized;
    resized[AGNC_TOOL_RESULT_MAX] = '\0';
    memcpy(resized + AGNC_TOOL_RESULT_MAX, suffix, sizeof(suffix));
}

static void agnc_query_report_repl_error(
    int chat_assistant_timestamp,
    agnc_status_t status,
    const char *detail)
{
    char line[512];

    if (!chat_assistant_timestamp) {
        return;
    }

    if (detail != NULL && detail[0] != '\0') {
        snprintf(line, sizeof(line), "%s", detail);
    } else {
        snprintf(line, sizeof(line), "query gagal (%s)", agnc_status_to_string(status));
    }

    agnc_console_print_chat_system(line);
}

static int agnc_query_emit_last_tool_fallback(
    const agnc_conversation_t *conversation,
    int chat_assistant_timestamp,
    int suppress_chat_output)
{
    size_t index;

    if (suppress_chat_output || conversation == NULL) {
        return 0;
    }

    for (index = conversation->count; index > 0; index--) {
        const agnc_conversation_message_t *message = agnc_conversation_at(conversation, index - 1);

        if (message == NULL || message->role == NULL) {
            continue;
        }
        if (strcmp(message->role, "user") == 0) {
            break;
        }
        if (strcmp(message->role, "tool") == 0 && message->content != NULL && message->content[0] != '\0') {
            if (chat_assistant_timestamp) {
                agnc_console_print_chat_system("model tanpa jawaban teks; hasil tool:");
                agnc_console_print_chat_assistant_begin();
            }
            agnc_console_print_assistant_body(message->content);
            return 1;
        }
    }

    return 0;
}

static agnc_status_t agnc_stream_callback(void *user_data, const char *chunk, size_t length)
{
    return agnc_sse_parser_feed((agnc_sse_parser_t *)user_data, chunk, length);
}

static void agnc_append_read_file_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *path_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

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
}

/* Helper generik: tambah property string ke object schema. */
static void agnc_schema_add_string_prop(
    yyjson_mut_doc *doc,
    yyjson_mut_val *properties_obj,
    const char *name,
    const char *description)
{
    yyjson_mut_val *prop = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, properties_obj, name, prop);
    yyjson_mut_obj_add_str(doc, prop, "type", "string");
    yyjson_mut_obj_add_str(doc, prop, "description", description);
}

static void agnc_append_write_file_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "write_file");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Write text content to a file (creates or overwrites).");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "path", "Absolute or relative path to the file.");
    agnc_schema_add_string_prop(doc, properties_obj, "content", "Full text content to write.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "path"));
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "content"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_edit_file_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "edit_file");
    yyjson_mut_obj_add_str(
        doc,
        function_obj,
        "description",
        "Replace exactly one unique old_string occurrence with new_string in a file.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "path", "Path to the file to edit.");
    agnc_schema_add_string_prop(doc, properties_obj, "old_string", "Exact text to replace (must be unique).");
    agnc_schema_add_string_prop(doc, properties_obj, "new_string", "Replacement text.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "path"));
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "old_string"));
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "new_string"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_grep_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "grep");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Search file contents with ripgrep (rg).");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "pattern", "Regex or literal search pattern.");
    agnc_schema_add_string_prop(
        doc,
        properties_obj,
        "path",
        "Directory or file to search. Omit or use src for source folder (default: src if exists, else repo root).");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "pattern"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_glob_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "glob");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Find files matching a glob pattern under a directory.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "pattern", "Glob pattern such as *.c or **/*.h.");
    agnc_schema_add_string_prop(doc, properties_obj, "path", "Directory to search (default .).");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "pattern"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_find_symbol_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "find_symbol");
    yyjson_mut_obj_add_str(
        doc,
        function_obj,
        "description",
        "Find function/type/variable definitions by symbol name using ctags (fast lookup).");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "name", "Symbol name to find (e.g. agnc_query_run).");
    agnc_schema_add_string_prop(
        doc,
        properties_obj,
        "path",
        "Optional path prefix filter (default .). Use src to limit to source folder.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "name"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_web_fetch_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "web_fetch");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Fetch a public HTTP or HTTPS URL and return text content.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "url", "Absolute http:// or https:// URL to fetch.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "url"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_todo_write_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *todos_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "todo_write");
    yyjson_mut_obj_add_str(
        doc,
        function_obj,
        "description",
        "Update the agent todo list (saved to ~/.agnc/todos.json).");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    yyjson_mut_obj_add_val(doc, properties_obj, "todos", todos_obj);
    yyjson_mut_obj_add_str(doc, todos_obj, "type", "array");
    yyjson_mut_obj_add_str(doc, todos_obj, "description", "Todo items with id, content, and status.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "todos"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_sub_agent_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "sub_agent");
    yyjson_mut_obj_add_str(
        doc,
        function_obj,
        "description",
        "Delegate a sub-task to an isolated agent loop; returns final assistant answer only.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    agnc_schema_add_string_prop(doc, properties_obj, "prompt", "Task prompt for the sub-agent.");
    agnc_schema_add_string_prop(
        doc,
        properties_obj,
        "max_iterations",
        "Optional cap on tool iterations for the sub-agent.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "prompt"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static void agnc_append_shell_tool(yyjson_mut_doc *doc, yyjson_mut_val *tools_arr)
{
    yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *function_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *properties_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *command_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *required_arr = yyjson_mut_arr(doc);

    yyjson_mut_arr_append(tools_arr, tool_obj);
    yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
    yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
    yyjson_mut_obj_add_str(doc, function_obj, "name", "shell");
    yyjson_mut_obj_add_str(doc, function_obj, "description", "Run a shell command and return stdout/stderr output.");
    yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
    yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
    yyjson_mut_obj_add_val(doc, parameters_obj, "properties", properties_obj);
    yyjson_mut_obj_add_val(doc, properties_obj, "command", command_obj);
    yyjson_mut_obj_add_str(doc, command_obj, "type", "string");
    yyjson_mut_obj_add_str(doc, command_obj, "description", "Shell command to execute.");
    yyjson_mut_arr_append(required_arr, yyjson_mut_str(doc, "command"));
    yyjson_mut_obj_add_val(doc, parameters_obj, "required", required_arr);
}

static int agnc_query_attach_json_object(
    yyjson_mut_doc *doc,
    yyjson_mut_val *parent,
    const char *key,
    const char *json_text)
{
    yyjson_doc *immutable_doc;
    yyjson_val *immutable_root;
    yyjson_mut_val *copy;

    if (json_text == NULL || json_text[0] == '\0') {
        return 0;
    }

    immutable_doc = yyjson_read(json_text, strlen(json_text), 0);
    if (immutable_doc == NULL) {
        return 0;
    }

    immutable_root = yyjson_doc_get_root(immutable_doc);
    copy = yyjson_val_mut_copy(doc, immutable_root);
    yyjson_doc_free(immutable_doc);
    if (copy == NULL) {
        return 0;
    }

    yyjson_mut_obj_add_val(doc, parent, key, copy);
    return 1;
}

static void agnc_append_mcp_tools(
    yyjson_mut_doc *doc,
    yyjson_mut_val *tools_arr,
    const agnc_mcp_tool_catalog_t *catalog)
{
    size_t index;

    if (catalog == NULL) {
        return;
    }

    for (index = 0; index < catalog->count; index++) {
        const agnc_mcp_runtime_tool_t *tool = &catalog->tools[index];
        yyjson_mut_val *tool_obj = yyjson_mut_obj(doc);
        yyjson_mut_val *function_obj = yyjson_mut_obj(doc);

        yyjson_mut_arr_append(tools_arr, tool_obj);
        yyjson_mut_obj_add_str(doc, tool_obj, "type", "function");
        yyjson_mut_obj_add_val(doc, tool_obj, "function", function_obj);
        yyjson_mut_obj_add_str(doc, function_obj, "name", tool->exposed_name);
        yyjson_mut_obj_add_str(
            doc,
            function_obj,
            "description",
            tool->description != NULL ? tool->description : "MCP tool");
        if (!agnc_query_attach_json_object(doc, function_obj, "parameters", tool->parameters_json)) {
            yyjson_mut_val *parameters_obj = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_val(doc, function_obj, "parameters", parameters_obj);
            yyjson_mut_obj_add_str(doc, parameters_obj, "type", "object");
            yyjson_mut_obj_add_obj(doc, parameters_obj, "properties");
        }
    }
}

static char *agnc_build_request_json(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    const agnc_mcp_tool_catalog_t *mcp_catalog)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *messages_arr;
    yyjson_mut_val *tools_arr;
    char *result;
    const agnc_gateway_descriptor_t *gateway;
    const char *api_model;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    gateway = agnc_registry_find_gateway(config->gateway_id);
    api_model = config->model;
    if (gateway != NULL && config->model != NULL) {
        api_model = agnc_provider_resolve_api_model(gateway, config->model);
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "model", api_model != NULL ? api_model : "");
    yyjson_mut_obj_add_bool(doc, root, "stream", config->stream ? true : false);

    messages_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "messages", messages_arr);

    if (conversation != NULL && conversation->count > 0) {
        size_t index;
        size_t tail_start;
        int has_system = conversation->items[0].role != NULL && strcmp(conversation->items[0].role, "system") == 0;
        int summary_added = 0;

        if (has_system) {
            const agnc_conversation_message_t *system_message = agnc_conversation_at(conversation, 0);
            yyjson_mut_val *entry = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_str(doc, entry, "role", system_message->role);
            yyjson_mut_obj_add_str(
                doc, entry, "content", system_message->content != NULL ? system_message->content : "");
            yyjson_mut_arr_append(messages_arr, entry);

            tail_start = conversation->count > 1 + AGNC_CONVERSATION_LLM_WINDOW
                ? conversation->count - AGNC_CONVERSATION_LLM_WINDOW
                : 1;
        } else {
            tail_start = conversation->count > AGNC_CONVERSATION_LLM_WINDOW
                ? conversation->count - AGNC_CONVERSATION_LLM_WINDOW
                : 0;
        }

        for (index = tail_start; index < conversation->count; index++) {
            const agnc_conversation_message_t *message = agnc_conversation_at(conversation, index);

            if (message == NULL) {
                continue;
            }

            if (!summary_added && index == tail_start && agnc_conversation_llm_needs_summary(conversation)) {
                yyjson_mut_val *summary_entry = yyjson_mut_obj(doc);
                char summary_text[512];
                const char *summary_body = conversation->history_summary;

                if (summary_body == NULL || summary_body[0] == '\0') {
                    snprintf(
                        summary_text,
                        sizeof(summary_text),
                        "[Ringkasan] %zu pesan sebelumnya tidak disertakan ke context model.",
                        conversation->memory_skipped > 0 ? conversation->memory_skipped : conversation->db_total -
                                                                                               conversation->count);
                    summary_body = summary_text;
                }

                yyjson_mut_obj_add_str(doc, summary_entry, "role", "user");
                yyjson_mut_obj_add_str(doc, summary_entry, "content", summary_body);
                yyjson_mut_arr_append(messages_arr, summary_entry);
                summary_added = 1;
            }

            {
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
                    yyjson_mut_obj_add_str(
                        doc, function, "arguments", message->tool_arguments != NULL ? message->tool_arguments : "{}");
                    if (message->content != NULL) {
                        yyjson_mut_obj_add_str(doc, entry, "content", message->content);
                    }
                } else {
                    yyjson_mut_obj_add_str(doc, entry, "content", message->content != NULL ? message->content : "");
                }

                yyjson_mut_arr_append(messages_arr, entry);
            }
        }
    }

    if (config->enable_tools || (mcp_catalog != NULL && mcp_catalog->count > 0)) {
        tools_arr = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, root, "tools", tools_arr);
        yyjson_mut_obj_add_str(doc, root, "tool_choice", "auto");

        if (config->tool_read_file) {
            agnc_append_read_file_tool(doc, tools_arr);
        }
        if (config->tool_shell) {
            agnc_append_shell_tool(doc, tools_arr);
        }
        if (config->tool_write_file) {
            agnc_append_write_file_tool(doc, tools_arr);
        }
        if (config->tool_edit_file) {
            agnc_append_edit_file_tool(doc, tools_arr);
        }
        if (config->tool_grep) {
            agnc_append_grep_tool(doc, tools_arr);
        }
        if (config->tool_glob) {
            agnc_append_glob_tool(doc, tools_arr);
        }
        if (config->tool_find_symbol) {
            agnc_append_find_symbol_tool(doc, tools_arr);
        }
        if (config->tool_web_fetch) {
            agnc_append_web_fetch_tool(doc, tools_arr);
        }
        if (config->tool_todo_write) {
            agnc_append_todo_write_tool(doc, tools_arr);
        }
        if (config->tool_sub_agent) {
            agnc_append_sub_agent_tool(doc, tools_arr);
        }

        agnc_append_mcp_tools(doc, tools_arr, mcp_catalog);
    }

    result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static void agnc_query_log_tool_line(const agnc_config_t *config, int interactive_repl, const char *line)
{
    if (line == NULL) {
        return;
    }

    if (interactive_repl) {
        agnc_console_print_chat_tool(line);
    } else if (config != NULL && config->verbose) {
        fprintf(stderr, "agnc: [tool] %s\n", line);
        fflush(stderr);
    }
}

static void agnc_execute_tool_invalidate_cache(const char *tool_name, agnc_status_t status)
{
    if (status != AGNC_STATUS_OK) {
        return;
    }

    if (strcmp(tool_name, "write_file") == 0 || strcmp(tool_name, "edit_file") == 0) {
        agnc_tool_cache_reset();
        agnc_find_symbol_index_invalidate();
    }
}

static void agnc_query_fill_hook_input(
    agnc_hook_payload_input_t *input,
    const agnc_config_t *config,
    const agnc_query_options_t *options)
{
    if (input == NULL) {
        return;
    }

    memset(input, 0, sizeof(*input));
    if (options != NULL) {
        input->session_name = options->session_name;
        if (options->usage_prompt_tokens != NULL) {
            input->usage_prompt = *options->usage_prompt_tokens;
        }
        if (options->usage_completion_tokens != NULL) {
            input->usage_completion = *options->usage_completion_tokens;
        }
        if (options->usage_total_tokens != NULL) {
            input->usage_total = *options->usage_total_tokens;
        }
    }

    if (config != NULL) {
        input->provider_id = config->provider_id;
        input->model = config->model;
    }
}

static void agnc_query_fire_hook(
    const agnc_config_t *config,
    agnc_hook_event_id_t event_id,
    const agnc_hook_payload_input_t *input,
    int *blocked_out)
{
    char *payload_json;

    if (config == NULL || !config->hooks_enabled) {
        return;
    }

    payload_json = agnc_hooks_build_payload_json(event_id, input);
    if (payload_json == NULL) {
        return;
    }

    (void)agnc_hooks_run(config, event_id, payload_json, blocked_out);
    agnc_hooks_free_payload(payload_json);
}

static agnc_status_t agnc_execute_tool(
    const agnc_config_t *config,
    const char *tool_name,
    char *tool_arguments,
    char **tool_result,
    int interactive_repl,
    int auto_approve,
    const agnc_mcp_registry_t *mcp_registry,
    const agnc_mcp_tool_catalog_t *mcp_catalog,
    agnc_mcp_session_t *mcp_session_reconnect,
    const agnc_query_options_t *query_options,
    int agent_depth)
{
    agnc_status_t status;
    int allowed = 1;

    *tool_result = NULL;

    if (agnc_tool_cache_is_eligible(tool_name) &&
        agnc_tool_cache_get(tool_name, tool_arguments, tool_result)) {
        return AGNC_STATUS_OK;
    }

    if (strcmp(tool_name, "read_file") == 0) {
        agnc_query_log_tool_line(config, interactive_repl, "read_file");
        status = agnc_tool_read_file_execute(tool_arguments, tool_result);
        if (status == AGNC_STATUS_OK) {
            (void)agnc_tool_cache_put(tool_name, tool_arguments, *tool_result);
        }
        return status;
    }

    if (strcmp(tool_name, "shell") == 0) {
        char *shell_command = NULL;
        const char *preview;
        char tool_line[512];

        status = agnc_tool_shell_extract_command(tool_arguments, &shell_command);
        preview = shell_command != NULL ? shell_command : agnc_tool_shell_command_preview(tool_arguments);

        snprintf(
            tool_line,
            sizeof(tool_line),
            "shell: %s",
            preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);

        if (shell_command != NULL && agnc_tool_shell_is_search_command(shell_command)) {
            free(shell_command);
            *tool_result = agnc_strdup_local(
                "error: shell search blocked; use the grep tool for text search or glob for file patterns.");
            return AGNC_STATUS_TOOL_FAILED;
        }
        free(shell_command);

        if (config->deny_shell_permission) {
            *tool_result = agnc_strdup_local("error: shell denied by config (always_deny)");
            return AGNC_STATUS_TOOL_DENIED;
        }

        if (config->ask_shell_permission && !auto_approve) {
            status = agnc_permission_ask_shell(
                agnc_tool_shell_command_preview(tool_arguments), &allowed, interactive_repl);
            if (status != AGNC_STATUS_OK) {
                return status;
            }
            if (!allowed) {
                *tool_result = agnc_strdup_local("error: shell command denied by user");
                return AGNC_STATUS_TOOL_FAILED;
            }
        }
        return agnc_tool_shell_execute(tool_arguments, tool_result);
    }

    if (strcmp(tool_name, "write_file") == 0) {
        const char *preview = agnc_tool_write_file_path_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "write_file: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);

        if (config->deny_write_permission) {
            *tool_result = agnc_strdup_local("error: write denied by config (always_deny)");
            return AGNC_STATUS_TOOL_DENIED;
        }

        if (config->ask_write_permission && !auto_approve) {
            status = agnc_permission_ask_file_write(preview, "write", &allowed, interactive_repl);
            if (status != AGNC_STATUS_OK) {
                return status;
            }
            if (!allowed) {
                *tool_result = agnc_strdup_local("error: write denied by user");
                return AGNC_STATUS_TOOL_FAILED;
            }
        }
        status = agnc_tool_write_file_execute(tool_arguments, tool_result);
        agnc_execute_tool_invalidate_cache(tool_name, status);
        return status;
    }

    if (strcmp(tool_name, "edit_file") == 0) {
        const char *preview = agnc_tool_edit_file_path_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "edit_file: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);

        if (config->deny_write_permission) {
            *tool_result = agnc_strdup_local("error: edit denied by config (always_deny)");
            return AGNC_STATUS_TOOL_DENIED;
        }

        if (config->ask_write_permission && !auto_approve) {
            status = agnc_permission_ask_file_write(preview, "edit", &allowed, interactive_repl);
            if (status != AGNC_STATUS_OK) {
                return status;
            }
            if (!allowed) {
                *tool_result = agnc_strdup_local("error: edit denied by user");
                return AGNC_STATUS_TOOL_FAILED;
            }
        }
        status = agnc_tool_edit_file_execute(tool_arguments, tool_result);
        agnc_execute_tool_invalidate_cache(tool_name, status);
        return status;
    }

    if (strcmp(tool_name, "grep") == 0) {
        const char *preview = agnc_tool_grep_pattern_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "grep: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);
        status = agnc_tool_grep_execute(tool_arguments, tool_result);
        if (status == AGNC_STATUS_OK) {
            (void)agnc_tool_cache_put(tool_name, tool_arguments, *tool_result);
        }
        return status;
    }

    if (strcmp(tool_name, "glob") == 0) {
        const char *preview = agnc_tool_glob_pattern_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "glob: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);
        status = agnc_tool_glob_execute(tool_arguments, tool_result);
        if (status == AGNC_STATUS_OK) {
            (void)agnc_tool_cache_put(tool_name, tool_arguments, *tool_result);
        }
        return status;
    }

    if (strcmp(tool_name, "find_symbol") == 0) {
        const char *preview = agnc_tool_find_symbol_name_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "find_symbol: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);
        status = agnc_tool_find_symbol_execute(tool_arguments, tool_result);
        if (status == AGNC_STATUS_OK) {
            (void)agnc_tool_cache_put(tool_name, tool_arguments, *tool_result);
        }
        return status;
    }

    if (strncmp(tool_name, "mcp_", 4) == 0) {
        const agnc_mcp_runtime_tool_t *runtime_tool;
        const agnc_mcp_connected_server_t *server;
        char tool_line[512];
        agnc_status_t call_status;

        runtime_tool = agnc_mcp_tool_catalog_find(mcp_catalog, tool_name);
        if (runtime_tool == NULL || mcp_registry == NULL) {
            *tool_result = agnc_strdup_local("error: unknown mcp tool");
            return AGNC_STATUS_TOOL_FAILED;
        }

        server = agnc_mcp_registry_server_at(mcp_registry, runtime_tool->server_index);
        if (server == NULL || !server->client.initialized) {
            *tool_result = agnc_strdup_local("error: mcp server not connected");
            return AGNC_STATUS_TOOL_FAILED;
        }

        snprintf(tool_line, sizeof(tool_line), "%s", tool_name);
        agnc_query_log_tool_line(config, interactive_repl, tool_line);

        if (config->deny_mcp_permission) {
            *tool_result = agnc_strdup_local("error: mcp tool denied by config (always_deny)");
            return AGNC_STATUS_TOOL_DENIED;
        }

        if (config->ask_mcp_permission && !auto_approve) {
            status = agnc_permission_ask_mcp(tool_name, &allowed, interactive_repl);
            if (status != AGNC_STATUS_OK) {
                return status;
            }
            if (!allowed) {
                *tool_result = agnc_strdup_local("error: mcp tool denied by user");
                return AGNC_STATUS_TOOL_FAILED;
            }
        }

        call_status = agnc_mcp_client_call_tool(
            (agnc_mcp_client_t *)&server->client,
            runtime_tool->mcp_tool_name,
            tool_arguments,
            tool_result,
            AGNC_MCP_TIMEOUT_MS);
        if (call_status != AGNC_STATUS_OK && mcp_session_reconnect != NULL) {
            agnc_status_t reconnect_status =
                agnc_mcp_session_reconnect(mcp_session_reconnect, config, AGNC_MCP_TIMEOUT_MS);

            if (reconnect_status == AGNC_STATUS_OK) {
                mcp_registry = &mcp_session_reconnect->registry;
                mcp_catalog = &mcp_session_reconnect->catalog;
                server = agnc_mcp_registry_server_at(mcp_registry, runtime_tool->server_index);
                if (server != NULL && server->client.initialized) {
                    call_status = agnc_mcp_client_call_tool(
                        (agnc_mcp_client_t *)&server->client,
                        runtime_tool->mcp_tool_name,
                        tool_arguments,
                        tool_result,
                        AGNC_MCP_TIMEOUT_MS);
                }
            }
        }

        if (call_status != AGNC_STATUS_OK) {
            if (*tool_result == NULL) {
                *tool_result = agnc_strdup_local("error: mcp tool call failed");
            }
            return AGNC_STATUS_TOOL_FAILED;
        }

        return AGNC_STATUS_OK;
    }

    if (strcmp(tool_name, "web_fetch") == 0) {
        const char *preview = agnc_tool_web_fetch_url_preview(tool_arguments);
        char tool_line[512];

        snprintf(tool_line, sizeof(tool_line), "web_fetch: %s", preview != NULL ? preview : "(empty)");
        agnc_query_log_tool_line(config, interactive_repl, tool_line);

        if (config->deny_web_fetch_permission) {
            *tool_result = agnc_strdup_local("error: web_fetch denied by config (always_deny)");
            return AGNC_STATUS_TOOL_DENIED;
        }

        if (config->ask_web_fetch_permission && !auto_approve) {
            status = agnc_permission_ask_web_fetch(preview, &allowed, interactive_repl);
            if (status != AGNC_STATUS_OK) {
                return status;
            }
            if (!allowed) {
                *tool_result = agnc_strdup_local("error: web_fetch denied by user");
                return AGNC_STATUS_TOOL_FAILED;
            }
        }

        return agnc_tool_web_fetch_execute(tool_arguments, tool_result);
    }

    if (strcmp(tool_name, "todo_write") == 0) {
        agnc_query_log_tool_line(config, interactive_repl, "todo_write");
        return agnc_tool_todo_write_execute(tool_arguments, tool_result);
    }

    if (strcmp(tool_name, "sub_agent") == 0) {
        agnc_query_log_tool_line(config, interactive_repl, "sub_agent");
        return agnc_tool_sub_agent_execute(config, query_options, agent_depth, tool_arguments, tool_result);
    }

    *tool_result = agnc_strdup_local("error: unsupported tool");
    return AGNC_STATUS_TOOL_FAILED;
}

static const char *agnc_query_system_text(const agnc_conversation_t *conversation)
{
    const agnc_conversation_message_t *msg;

    if (conversation == NULL || conversation->count == 0) {
        return NULL;
    }

    msg = agnc_conversation_at(conversation, 0);
    if (msg == NULL || msg->role == NULL || strcmp(msg->role, "system") != 0) {
        return NULL;
    }

    return msg->content;
}

static const char *agnc_query_last_user_text(const agnc_conversation_t *conversation)
{
    size_t index;

    if (conversation == NULL) {
        return NULL;
    }

    for (index = conversation->count; index > 0; index--) {
        const agnc_conversation_message_t *msg = agnc_conversation_at(conversation, index - 1);

        if (msg != NULL && msg->role != NULL && strcmp(msg->role, "user") == 0) {
            return msg->content;
        }
    }

    return NULL;
}

static agnc_status_t agnc_run_provider_turn(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    agnc_sse_parser_t *parser,
    char **error_message,
    volatile int *cancel_flag,
    const agnc_mcp_tool_catalog_t *mcp_catalog,
    const char *session_sqlite_path)
{
    const agnc_gateway_descriptor_t *gateway;
    char *url;
    char *auth_header;
    char *request_json;
    agnc_status_t status;

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (gateway->transport_kind == AGNC_TRANSPORT_OPENCODE_NATIVE) {
        const char *system_text = agnc_query_system_text(conversation);
        const char *user_text = agnc_query_last_user_text(conversation);

        if (user_text == NULL || user_text[0] == '\0') {
            return AGNC_STATUS_INVALID_ARGUMENT;
        }

        return agnc_opencode_run_turn(
            config,
            session_sqlite_path,
            system_text,
            user_text,
            parser,
            error_message,
            cancel_flag);
    }

    if (gateway->transport_kind == AGNC_TRANSPORT_ANTHROPIC_NATIVE) {
        return agnc_anthropic_run_turn(
            config,
            conversation,
            mcp_catalog,
            parser,
            error_message,
            cancel_flag);
    }

    url = agnc_provider_build_chat_url(gateway, config->base_url);
    if (url == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    auth_header = NULL;
    if (config->api_key != NULL && config->api_key[0] != '\0') {
        auth_header = agnc_provider_build_auth_header(gateway, config->api_key);
        if (auth_header == NULL) {
            free(url);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    } else if (gateway->requires_auth) {
        free(url);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    request_json = agnc_build_request_json(config, conversation, mcp_catalog);
    if (request_json == NULL) {
        free(auth_header);
        free(url);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (config->verbose) {
        fprintf(stderr, "agnc: POST %s\n", url);
    }

    status = agnc_http_post_stream(
        url, auth_header, request_json, agnc_stream_callback, parser, error_message, cancel_flag);

    if (status == AGNC_STATUS_OK && agnc_sse_parser_get_error(parser) != NULL) {
        if (error_message != NULL && *error_message == NULL) {
            *error_message = agnc_strdup_local(agnc_sse_parser_get_error(parser));
        }
        status = AGNC_STATUS_PROVIDER_ERROR;
    }

    if (status == AGNC_STATUS_OK) {
        status = agnc_sse_parser_flush(parser);
    }

    free(request_json);
    free(auth_header);
    free(url);
    return status;
}

/* Deteksi prompt pencarian kode agar tool shell dinonaktifkan (model pakai grep, bukan findstr). */
static int agnc_query_prompt_is_search(const char *prompt)
{
    char lower[512] = {0}; /* Inisialisasi eksplisit: linter lnt-uninitialized-local. */
    size_t index;
    size_t length;

    if (prompt == NULL || prompt[0] == '\0') {
        return 0;
    }

    length = strlen(prompt);
    if (length >= sizeof(lower)) {
        length = sizeof(lower) - 1;
    }

    for (index = 0; index < length; index++) {
        char ch = prompt[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        lower[index] = ch;
    }
    lower[length] = '\0';

    if (strstr(lower, "grep") != NULL) {
        return 1;
    }
    if (strstr(lower, "pattern") != NULL) {
        return 1;
    }
    if (strstr(lower, "findstr") != NULL) {
        return 1;
    }
    if (strstr(lower, "ripgrep") != NULL) {
        return 1;
    }
    if (strstr(lower, "cari ") != NULL) {
        return 1;
    }
    if (strstr(lower, "search ") != NULL || strstr(lower, " search") != NULL) {
        return 1;
    }

    return 0;
}

const char *agnc_query_default_system_prompt(int enable_tools, int search_only)
{
    if (enable_tools) {
        if (search_only) {
            return "You are a helpful coding assistant on Windows. "
                   "This is a search-only query: use the grep tool (path=src if user mentions src). "
                   "Shell is disabled. Never suggest findstr, where, or rg via shell. "
                   "Match the user's language. Be concise.";
        }
        return "You are a helpful coding assistant on Windows. "
               "For code/text search use the grep tool (ripgrep), never shell findstr/find/rg/where. "
               "For symbol definitions (functions, types) use find_symbol; for file name patterns use glob. "
               "Use read_file, write_file, edit_file for file content. "
               "Use shell only for directory listings with short commands like `dir` (avoid recursive -R). "
               "Paths like src/ resolve from repo root even when cwd is a build subfolder. "
               "For directory or file listings use a bullet or numbered list with one path or name per line; "
               "never comma-join multiple items on the same line. "
               "Match the user's language. Be concise; do not add filler intros before answers.";
    }
    return "You are a helpful coding assistant. You cannot access the filesystem or run shell commands in this session. "
           "Match the user's language. Be concise; do not invent file names or directory listings.";
}

/*
 * Konteks produk agnc: host CLI, lokasi config/sessions, beda workspace tool vs MCP.
 * Di-inject ke system prompt agar model tidak mengarang Claude Desktop / MCP host lain.
 */
static char *agnc_query_build_product_context(const char *workspace_root)
{
    char *config_path = NULL;
    char *sessions_dir = NULL;
    char *context = NULL;
    size_t length;
    const char *workspace_env = getenv("AGNC_WORKSPACE");
    const char *workspace_label = workspace_root != NULL ? workspace_root : "(unknown)";

    (void)agnc_path_default_config(&config_path);
    (void)agnc_session_default_dir(&sessions_dir);

    length = 1536;
    if (config_path != NULL) {
        length += strlen(config_path);
    }
    if (sessions_dir != NULL) {
        length += strlen(sessions_dir);
    }
    if (workspace_label != NULL) {
        length += strlen(workspace_label);
    }

    context = (char *)malloc(length);
    if (context == NULL) {
        free(config_path);
        free(sessions_dir);
        return NULL;
    }

    snprintf(
        context,
        length,
        "Host: agnc CLI on Windows (not Claude Desktop, Cursor, or VS Code as MCP host). "
        "Global config file: %s (outside repo workspace; read with read_file using this absolute path). "
        "Session storage: %s (SQLite per session; active pointer in active.txt). "
        "Tool workspace (read_file/grep/glob/write): %s%s. "
        "To change tool workspace: set env AGNC_WORKSPACE to a directory and restart agnc, or run agnc from another repo root. "
        "MCP filesystem roots are separate: edit mcp.servers[].args in global config, then /mcp reconnect in REPL. "
        "Do not invent claude_desktop_config.json or host-app MCP paths unless the user names that product.",
        config_path != NULL ? config_path : "~/.agnc.json",
        sessions_dir != NULL ? sessions_dir : "~/.agnc/sessions",
        workspace_label,
        workspace_env != NULL && workspace_env[0] != '\0' ? " (AGNC_WORKSPACE override active)" : "");

    free(config_path);
    free(sessions_dir);
    return context;
}

static char *agnc_query_build_system_prompt(const agnc_config_t *config, int enable_tools, int search_only)
{
    const char *base = agnc_query_default_system_prompt(enable_tools, search_only);
    char *workspace_root = NULL;
    char *product_context = NULL;
    char *skills_context = NULL;
    char *prompt = NULL;
    size_t length;

    if (!enable_tools) {
        return agnc_strdup_local(base);
    }

    if (agnc_tool_path_workspace_root(&workspace_root) != AGNC_STATUS_OK || workspace_root == NULL) {
        return agnc_strdup_local(base);
    }

    product_context = agnc_query_build_product_context(workspace_root);
    if (config != NULL) {
        (void)agnc_skills_build_context(config, &skills_context);
    }

    length = strlen(base) + strlen(workspace_root) + (product_context != NULL ? strlen(product_context) : 0) +
             (skills_context != NULL ? strlen(skills_context) : 0) + 192;
    prompt = (char *)malloc(length);
    if (prompt == NULL) {
        free(workspace_root);
        free(product_context);
        free(skills_context);
        return agnc_strdup_local(base);
    }

    snprintf(
        prompt,
        length,
        "%s %s %s Workspace root: %s. For workspace-wide file counts use glob with path \".\"; "
        "use shell dir only when the user names a specific folder.",
        base,
        product_context != NULL ? product_context : "",
        skills_context != NULL ? skills_context : "",
        workspace_root);
    free(workspace_root);
    free(product_context);
    free(skills_context);
    return prompt;
}

agnc_status_t agnc_query_run(
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    const char *user_prompt,
    const agnc_query_options_t *options)
{
    agnc_sse_parser_t parser;
    agnc_status_t status;
    size_t iteration;
    char *error_message = NULL;
    int curl_initialized = 0;
    int any_output = 0;
    int search_only = 0;
    volatile int *cancel_flag = NULL;
    int stream_live_print = 0;
    int chat_assistant_timestamp = 0;
    int suppress_chat_output = 0;
    int auto_approve = 0;
    int agent_depth = 0;
    char *system_prompt = NULL;
    agnc_mcp_registry_t local_registry;
    agnc_mcp_tool_catalog_t local_catalog;
    const agnc_mcp_registry_t *mcp_registry = NULL;
    const agnc_mcp_tool_catalog_t *mcp_catalog = NULL;
    int owns_ephemeral_mcp = 0;
    const char *session_sqlite_path = NULL;
    int tools_for_prompt;

    if (config == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (options != NULL) {
        cancel_flag = options->cancel_flag;
        stream_live_print = options->stream_live_print;
        chat_assistant_timestamp = options->chat_assistant_timestamp;
        suppress_chat_output = options->suppress_chat_output;
        auto_approve = options->auto_approve;
        agent_depth = options->agent_depth;

        if (options->usage_prompt_tokens != NULL) {
            *options->usage_prompt_tokens = -1;
        }
        if (options->usage_completion_tokens != NULL) {
            *options->usage_completion_tokens = -1;
        }
        if (options->usage_total_tokens != NULL) {
            *options->usage_total_tokens = -1;
        }

        session_sqlite_path = options->session_sqlite_path;

        if (options->mcp_session != NULL) {
            (void)agnc_mcp_session_ensure(options->mcp_session, config, AGNC_MCP_TIMEOUT_MS);
            mcp_registry = &options->mcp_session->registry;
            mcp_catalog = &options->mcp_session->catalog;
        }
    }

    if (mcp_registry == NULL && config->mcp_server_count > 0) {
        agnc_mcp_registry_init(&local_registry);
        agnc_mcp_tool_catalog_init(&local_catalog);
        (void)agnc_mcp_registry_load_from_config(config, &local_registry, AGNC_MCP_TIMEOUT_MS);
        (void)agnc_mcp_tool_catalog_build(&local_registry, &local_catalog);
        mcp_registry = &local_registry;
        mcp_catalog = &local_catalog;
        owns_ephemeral_mcp = 1;
    }

    if (user_prompt != NULL && user_prompt[0] != '\0') {
        search_only = agnc_query_prompt_is_search(user_prompt);
        if (search_only) {
            config->tool_shell = 0;
        }

        status = agnc_conversation_push(conversation, "user", user_prompt, NULL, NULL, NULL);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    {
        const agnc_gateway_descriptor_t *gateway = agnc_registry_find_gateway(config->gateway_id);

        tools_for_prompt = config->enable_tools;
        if (gateway != NULL && gateway->transport_kind == AGNC_TRANSPORT_OPENCODE_NATIVE) {
            tools_for_prompt = 0;
        }
    }

    system_prompt = agnc_query_build_system_prompt(config, tools_for_prompt, search_only);
    status = agnc_conversation_ensure_system(conversation, system_prompt);
    free(system_prompt);
    system_prompt = NULL;
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (user_prompt != NULL && user_prompt[0] != '\0') {
        agnc_hook_payload_input_t hook_input;
        agnc_query_fill_hook_input(&hook_input, config, options);
        hook_input.user_prompt = user_prompt;
        agnc_query_fire_hook(config, AGNC_HOOK_EVENT_PRE_TURN, &hook_input, NULL);
    }

    memset(&parser, 0, sizeof(parser));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
        curl_initialized = 1;
    }

    for (iteration = 0; iteration < (size_t)config->max_tool_iterations; iteration++) {
        const agnc_sse_tool_call_t *tool_call;

        agnc_sse_parser_free(&parser);
        agnc_sse_parser_init(&parser, config->stream, config->verbose);
        if (options != NULL && options->stream_delta_fn != NULL) {
            agnc_sse_parser_set_delta_callback(
                &parser, options->stream_delta_fn, options->stream_delta_ctx);
        } else {
            agnc_sse_parser_set_live_print(&parser, stream_live_print);
        }

        int saved_sub_agent = config->tool_sub_agent;

        if (chat_assistant_timestamp && !suppress_chat_output) {
            agnc_console_spinner_start();
        }

        if (agent_depth > 0) {
            config->tool_sub_agent = 0;
        }

        status = agnc_run_provider_turn(
            config,
            conversation,
            &parser,
            &error_message,
            cancel_flag,
            mcp_catalog,
            session_sqlite_path);

        config->tool_sub_agent = saved_sub_agent;

        if (chat_assistant_timestamp && !suppress_chat_output) {
            agnc_console_spinner_stop();
        }

        if (status != AGNC_STATUS_OK) {
            goto cleanup;
        }

        agnc_query_accumulate_usage(&parser, options);
        agnc_query_accumulate_cost(config, &parser, options);

        agnc_sse_parser_finalize_turn(&parser);

        if (!agnc_sse_parser_has_tool_calls(&parser) || agnc_sse_parser_get_tool_call_count(&parser) == 0) {
            const char *content = agnc_sse_parser_get_content(&parser);
            int has_content = content != NULL && content[0] != '\0';

            if (!suppress_chat_output && (has_content || agnc_sse_parser_printed_any(&parser))) {
                if (chat_assistant_timestamp) {
                    agnc_console_print_chat_assistant_begin();
                }
                if (has_content) {
                    agnc_console_print_assistant_body(content);
                }
                any_output = 1;
            }

            if (has_content) {
                status = agnc_conversation_push(conversation, "assistant", content, NULL, NULL, NULL);
                if (status != AGNC_STATUS_OK) {
                    goto cleanup;
                }
            }

            if (any_output && !chat_assistant_timestamp && !suppress_chat_output) {
                fputc('\n', stdout);
            }
            status = AGNC_STATUS_OK;
            goto cleanup;
        }

        if (agnc_sse_parser_get_tool_call_count(&parser) > 1 && config->verbose) {
            fprintf(stderr, "agnc: warning: multiple tool calls received, using the first one\n");
        }

        tool_call = agnc_sse_parser_get_tool_call(&parser, 0);
        if (tool_call == NULL || tool_call->name == NULL) {
            status = AGNC_STATUS_PROVIDER_ERROR;
            if (error_message == NULL) {
                error_message = agnc_strdup_local("tool call missing function name");
            }
            goto cleanup;
        }

        {
            char synthetic_id[48];
            char *arguments = tool_call->arguments != NULL ? agnc_strdup_local(tool_call->arguments) : NULL;
            char *tool_result = NULL;
            const char *tool_id = tool_call->id;

            if (tool_id == NULL) {
                snprintf(synthetic_id, sizeof(synthetic_id), "call_%zu", iteration);
                tool_id = synthetic_id;
                if (config->verbose) {
                    fprintf(stderr, "agnc: warning: tool call missing id, using %s\n", synthetic_id);
                }
            }

            if (arguments != NULL) {
                arguments = agnc_sse_tool_arguments_finalize(arguments);
            }

            if (config->verbose) {
                fprintf(stderr, "agnc: tool call %s(%s)\n", tool_call->name, arguments != NULL ? arguments : "{}");
            }

            status = agnc_conversation_push(
                conversation, "assistant", NULL, tool_id, tool_call->name, arguments);
            if (status != AGNC_STATUS_OK) {
                free(arguments);
                goto cleanup;
            }

            {
                agnc_hook_payload_input_t hook_input;
                int hook_blocked = 0;

                agnc_query_fill_hook_input(&hook_input, config, options);
                hook_input.tool_name = tool_call->name;
                hook_input.tool_arguments = arguments;
                agnc_query_fire_hook(config, AGNC_HOOK_EVENT_PRE_TOOL, &hook_input, &hook_blocked);
                if (hook_blocked) {
                    free(arguments);
                    tool_result = agnc_strdup_local("error: tool blocked by pre_tool hook");
                    status = AGNC_STATUS_TOOL_DENIED;
                    if (tool_result == NULL) {
                        status = AGNC_STATUS_OUT_OF_MEMORY;
                        goto cleanup;
                    }
                    agnc_query_truncate_tool_result(&tool_result);
                    status = agnc_conversation_push(
                        conversation,
                        "tool",
                        tool_result,
                        tool_id,
                        NULL,
                        NULL);
                    free(tool_result);
                    if (status != AGNC_STATUS_OK) {
                        goto cleanup;
                    }
                    continue;
                }
            }

            status = agnc_execute_tool(
                config,
                tool_call->name,
                arguments,
                &tool_result,
                chat_assistant_timestamp,
                auto_approve,
                mcp_registry,
                mcp_catalog,
                options != NULL ? options->mcp_session : NULL,
                options,
                agent_depth);
            free(arguments);

            if (tool_result == NULL) {
                if (status == AGNC_STATUS_OK) {
                    status = AGNC_STATUS_TOOL_FAILED;
                }
                tool_result = agnc_strdup_local("error: tool execution failed");
                if (tool_result == NULL) {
                    status = AGNC_STATUS_OUT_OF_MEMORY;
                    goto cleanup;
                }
            }

            agnc_query_truncate_tool_result(&tool_result);

            {
                agnc_hook_payload_input_t hook_input;
                agnc_query_fill_hook_input(&hook_input, config, options);
                hook_input.tool_name = tool_call->name;
                hook_input.tool_arguments = arguments;
                hook_input.tool_status = agnc_status_to_string(status);
                agnc_query_fire_hook(config, AGNC_HOOK_EVENT_POST_TOOL, &hook_input, NULL);
            }

            status = agnc_conversation_push(
                conversation,
                "tool",
                tool_result != NULL ? tool_result : "error: tool failed",
                tool_id,
                NULL,
                NULL);
            free(tool_result);

            if (status != AGNC_STATUS_OK) {
                goto cleanup;
            }
        }
    }

    status = AGNC_STATUS_PROVIDER_ERROR;
    fprintf(stderr, "agnc: max tool iterations reached\n");

cleanup:
    if (status == AGNC_STATUS_OK && user_prompt != NULL && user_prompt[0] != '\0') {
        agnc_hook_payload_input_t hook_input;
        agnc_query_fill_hook_input(&hook_input, config, options);
        hook_input.user_prompt = user_prompt;
        agnc_query_fire_hook(config, AGNC_HOOK_EVENT_POST_TURN, &hook_input, NULL);
    }

    if (chat_assistant_timestamp && !suppress_chat_output) {
        agnc_console_spinner_stop();
    }

    if (status == AGNC_STATUS_CANCELLED) {
        if (!suppress_chat_output) {
            fputc('\n', stdout);
        }
        fprintf(stderr, "agnc: request cancelled\n");
    } else if (status == AGNC_STATUS_OK && !any_output && !suppress_chat_output) {
        if (agnc_query_emit_last_tool_fallback(conversation, chat_assistant_timestamp, suppress_chat_output)) {
            any_output = 1;
        } else {
            fprintf(stderr, "agnc: warning: provider returned no visible output (enable runtime.verbose for details)\n");
            if (chat_assistant_timestamp) {
                agnc_console_print_chat_system(
                    "model tidak mengembalikan jawaban (aktifkan runtime.verbose atau ulangi prompt)");
            }
        }
    }

    if (status != AGNC_STATUS_OK && status != AGNC_STATUS_CANCELLED) {
        if (error_message != NULL) {
            fprintf(stderr, "agnc: %s\n", error_message);
        } else if (agnc_sse_parser_get_error(&parser) != NULL) {
            fprintf(stderr, "agnc: %s\n", agnc_sse_parser_get_error(&parser));
        }
        if (options != NULL && options->error_message_out != NULL && *options->error_message_out == NULL) {
            if (error_message != NULL) {
                *options->error_message_out = agnc_strdup_local(error_message);
            } else if (agnc_sse_parser_get_error(&parser) != NULL) {
                *options->error_message_out = agnc_strdup_local(agnc_sse_parser_get_error(&parser));
            }
        }
        agnc_query_report_repl_error(
            chat_assistant_timestamp,
            status,
            error_message != NULL ? error_message : agnc_sse_parser_get_error(&parser));
    }

    free(error_message);
    agnc_sse_parser_free(&parser);

    if (curl_initialized) {
        curl_global_cleanup();
    }

    if (owns_ephemeral_mcp) {
        agnc_mcp_tool_catalog_free(&local_catalog);
        agnc_mcp_registry_free(&local_registry);
    }

    return status;
}

agnc_status_t agnc_query_print(
    const agnc_config_t *config,
    const char *prompt,
    const agnc_query_options_t *options)
{
    agnc_config_t run_config;
    agnc_conversation_t conversation;
    agnc_query_options_t local_options;
    agnc_status_t status;

    if (config == NULL || prompt == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    run_config = *config;
    run_config.stream = 0;

    agnc_conversation_init(&conversation);

    memset(&local_options, 0, sizeof(local_options));
    if (options != NULL) {
        local_options = *options;
    }
    local_options.stream_live_print = 0;

    status = agnc_query_run(&run_config, &conversation, prompt, &local_options);
    agnc_conversation_clear(&conversation);
    return status;
}
