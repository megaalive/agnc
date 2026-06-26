/*
 * query.c
 *
 * Agent loop Fase 1: streaming OpenAI-compatible + eksekusi read_file dan shell.
 */

#include "agnc/query.h"
#include "agnc/console.h"
#include "agnc/net/http.h"
#include "agnc/net/sse.h"
#include "agnc/permissions.h"
#include "agnc/tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <yyjson.h>

#define AGNC_MAX_MESSAGES 64

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    char *tool_name;
    char *tool_arguments;
} agnc_chat_message_t;

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

static agnc_status_t agnc_stream_callback(void *user_data, const char *chunk, size_t length)
{
    return agnc_sse_parser_feed((agnc_sse_parser_t *)user_data, chunk, length);
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

static char *agnc_build_request_json(const agnc_config_t *config, const agnc_message_list_t *messages)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *messages_arr;
    yyjson_mut_val *tools_arr;
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

    if (config->enable_tools) {
        tools_arr = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, root, "tools", tools_arr);
        yyjson_mut_obj_add_str(doc, root, "tool_choice", "auto");

        if (config->tool_read_file) {
            agnc_append_read_file_tool(doc, tools_arr);
        }
        if (config->tool_shell) {
            agnc_append_shell_tool(doc, tools_arr);
        }
    }

    result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static agnc_status_t agnc_execute_tool(
    const agnc_config_t *config,
    const char *tool_name,
    char *tool_arguments,
    char **tool_result)
{
    agnc_status_t status;
    int allowed = 1;

    *tool_result = NULL;

    if (strcmp(tool_name, "read_file") == 0) {
        fprintf(stderr, "agnc: [tool] read_file\n");
        return agnc_tool_read_file_execute(tool_arguments, tool_result);
    }

    if (strcmp(tool_name, "shell") == 0) {
        const char *preview = agnc_tool_shell_command_preview(tool_arguments);

        fprintf(stderr, "agnc: [tool] shell: %s\n", preview != NULL ? preview : "(empty)");

        if (config->ask_shell_permission) {
            status = agnc_permission_ask_shell(agnc_tool_shell_command_preview(tool_arguments), &allowed);
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

    *tool_result = agnc_strdup_local("error: unsupported tool");
    return AGNC_STATUS_TOOL_FAILED;
}

static agnc_status_t agnc_run_provider_turn(
    const agnc_config_t *config,
    const agnc_message_list_t *messages,
    agnc_sse_parser_t *parser,
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

agnc_status_t agnc_query_print(const agnc_config_t *config, const char *prompt)
{
    agnc_message_list_t messages;
    agnc_sse_parser_t parser;
    agnc_status_t status;
    size_t iteration;
    char *error_message = NULL;
    int curl_initialized = 0;
    int any_output = 0;
    const char *system_prompt;

    if (config == NULL || prompt == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    memset(&parser, 0, sizeof(parser));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
        curl_initialized = 1;
    }

    agnc_message_list_init(&messages);

    if (config->enable_tools) {
        system_prompt =
            "You are a helpful coding assistant on Windows. "
            "Use read_file to read files. For directory listings use shell with short commands "
            "like `dir` or `Get-ChildItem` (avoid recursive flags like -R). "
            "Match the user's language. Be concise; do not add filler intros before answers.";
    } else {
        system_prompt =
            "You are a helpful coding assistant. You cannot access the filesystem or run shell commands in this session. "
            "Match the user's language. Be concise; do not invent file names or directory listings.";
    }

    status = agnc_message_list_push(&messages, "system", system_prompt, NULL, NULL, NULL);
    if (status != AGNC_STATUS_OK) {
        goto cleanup;
    }

    status = agnc_message_list_push(&messages, "user", prompt, NULL, NULL, NULL);
    if (status != AGNC_STATUS_OK) {
        goto cleanup;
    }

    for (iteration = 0; iteration < (size_t)config->max_tool_iterations; iteration++) {
        const agnc_sse_tool_call_t *tool_call;

        agnc_sse_parser_free(&parser);
        agnc_sse_parser_init(&parser, config->stream, config->verbose);

        status = agnc_run_provider_turn(config, &messages, &parser, &error_message);
        if (status != AGNC_STATUS_OK) {
            goto cleanup;
        }

        agnc_sse_parser_finalize_turn(&parser);

        /* Render markdown/tabel ASCII; modul SSE hanya menandai ada konten. */
        if (agnc_sse_parser_printed_any(&parser)) {
            agnc_console_print_assistant_body(agnc_sse_parser_get_content(&parser));
        }

        if (agnc_sse_parser_printed_any(&parser)) {
            any_output = 1;
        }

        if (!agnc_sse_parser_has_tool_calls(&parser) || agnc_sse_parser_get_tool_call_count(&parser) == 0) {
            if (any_output) {
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

            status = agnc_message_list_push(&messages, "assistant", NULL, tool_id, tool_call->name, arguments);
            if (status != AGNC_STATUS_OK) {
                free(arguments);
                goto cleanup;
            }

            status = agnc_execute_tool(config, tool_call->name, arguments, &tool_result);
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

            status = agnc_message_list_push(
                &messages,
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

        if (agnc_sse_parser_printed_any(&parser)) {
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
        } else if (agnc_sse_parser_get_error(&parser) != NULL) {
            fprintf(stderr, "agnc: %s\n", agnc_sse_parser_get_error(&parser));
        }
    }

    free(error_message);
    agnc_sse_parser_free(&parser);
    agnc_message_list_clear(&messages);

    if (curl_initialized) {
        curl_global_cleanup();
    }

    return status;
}
