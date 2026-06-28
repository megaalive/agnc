/*
 * anthropic.c
 *
 * Client native Anthropic Messages API.
 */

#include "agnc/anthropic.h"

#include "agnc/conversation.h"
#include "agnc/net/http.h"
#include "agnc/provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_ANTHROPIC_VERSION "anthropic-version: 2023-06-01"

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_anthropic_join_url(const char *base_url, const char *path)
{
    size_t base_len;
    size_t path_len;
    char *result;
    int needs_slash = 0;

    if (base_url == NULL || path == NULL) {
        return NULL;
    }

    base_len = strlen(base_url);
    while (base_len > 0 && base_url[base_len - 1] == '/') {
        base_len--;
    }
    path_len = strlen(path);
    while (path_len > 0 && path[0] == '/') {
        path++;
        path_len--;
    }
    needs_slash = base_len > 0 && path_len > 0;
    result = (char *)malloc(base_len + path_len + needs_slash + 1);
    if (result == NULL) {
        return NULL;
    }

    memcpy(result, base_url, base_len);
    if (needs_slash) {
        result[base_len] = '/';
        memcpy(result + base_len + 1, path, path_len + 1);
    } else {
        memcpy(result + base_len, path, path_len + 1);
    }

    return result;
}

static const char *agnc_anthropic_api_model(const agnc_config_t *config)
{
    const agnc_gateway_descriptor_t *gateway;

    if (config == NULL || config->model == NULL) {
        return NULL;
    }

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return config->model;
    }

    return agnc_provider_resolve_api_model(gateway, config->model);
}

static void agnc_anthropic_append_tool(
    yyjson_mut_doc *doc,
    yyjson_mut_val *tools_arr,
    const char *name,
    const char *description,
    const char *required_prop)
{
    yyjson_mut_val *tool = yyjson_mut_obj(doc);
    yyjson_mut_val *schema = yyjson_mut_obj(doc);
    yyjson_mut_val *props = yyjson_mut_obj(doc);
    yyjson_mut_val *prop = yyjson_mut_obj(doc);
    yyjson_mut_val *required = yyjson_mut_arr(doc);

    yyjson_mut_obj_add_str(doc, tool, "name", name);
    yyjson_mut_obj_add_str(doc, tool, "description", description);
    yyjson_mut_obj_add_val(doc, tool, "input_schema", schema);
    yyjson_mut_obj_add_str(doc, schema, "type", "object");
    yyjson_mut_obj_add_val(doc, schema, "properties", props);
    yyjson_mut_obj_add_val(doc, props, required_prop, prop);
    yyjson_mut_obj_add_str(doc, prop, "type", "string");
    yyjson_mut_arr_append(required, yyjson_mut_str(doc, required_prop));
    yyjson_mut_obj_add_val(doc, schema, "required", required);
    yyjson_mut_arr_append(tools_arr, tool);
}

static char *agnc_anthropic_build_request_json(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    const agnc_mcp_tool_catalog_t *mcp_catalog)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *messages;
    size_t index;
    const char *system_text = NULL;
    char *json_text;

    (void)mcp_catalog;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "model", agnc_anthropic_api_model(config));
    yyjson_mut_obj_add_int(doc, root, "max_tokens", 8192);
    messages = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "messages", messages);

    for (index = 0; conversation != NULL && index < conversation->count; index++) {
        const agnc_conversation_message_t *message = agnc_conversation_at(conversation, index);

        if (message == NULL || message->role == NULL) {
            continue;
        }

        if (strcmp(message->role, "system") == 0) {
            if (message->content != NULL && message->content[0] != '\0') {
                system_text = message->content;
            }
            continue;
        }

        if (strcmp(message->role, "tool") == 0) {
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_val *content_arr = yyjson_mut_arr(doc);
            yyjson_mut_val *block = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_str(doc, entry, "role", "user");
            yyjson_mut_obj_add_val(doc, entry, "content", content_arr);
            yyjson_mut_obj_add_str(doc, block, "type", "tool_result");
            yyjson_mut_obj_add_str(doc, block, "tool_use_id", message->tool_call_id != NULL ? message->tool_call_id : "");
            yyjson_mut_obj_add_str(doc, block, "content", message->content != NULL ? message->content : "");
            yyjson_mut_arr_append(content_arr, block);
            yyjson_mut_arr_append(messages, entry);
            continue;
        }

        if (message->tool_name != NULL) {
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_val *content_arr = yyjson_mut_arr(doc);
            yyjson_mut_val *block = yyjson_mut_obj(doc);
            yyjson_mut_val *input_obj = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_str(doc, entry, "role", "assistant");
            yyjson_mut_obj_add_val(doc, entry, "content", content_arr);
            yyjson_mut_obj_add_str(doc, block, "type", "tool_use");
            yyjson_mut_obj_add_str(doc, block, "id", message->tool_call_id != NULL ? message->tool_call_id : "call");
            yyjson_mut_obj_add_str(doc, block, "name", message->tool_name);
            yyjson_mut_obj_add_val(doc, block, "input", input_obj);
            yyjson_mut_arr_append(content_arr, block);
            yyjson_mut_arr_append(messages, entry);
            continue;
        }

        if (strcmp(message->role, "user") == 0 || strcmp(message->role, "assistant") == 0) {
            yyjson_mut_val *entry = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_str(doc, entry, "role", message->role);
            yyjson_mut_obj_add_str(doc, entry, "content", message->content != NULL ? message->content : "");
            yyjson_mut_arr_append(messages, entry);
        }
    }

    if (system_text != NULL) {
        yyjson_mut_obj_add_str(doc, root, "system", system_text);
    }

    if (config->enable_tools) {
        yyjson_mut_val *tools_arr = yyjson_mut_arr(doc);

        yyjson_mut_obj_add_val(doc, root, "tools", tools_arr);
        if (config->tool_read_file) {
            agnc_anthropic_append_tool(doc, tools_arr, "read_file", "Read a text file.", "path");
        }
        if (config->tool_grep) {
            agnc_anthropic_append_tool(doc, tools_arr, "grep", "Search with ripgrep.", "pattern");
        }
        if (config->tool_glob) {
            agnc_anthropic_append_tool(doc, tools_arr, "glob", "Find files by glob.", "pattern");
        }
        if (config->tool_shell) {
            agnc_anthropic_append_tool(doc, tools_arr, "shell", "Run shell command.", "command");
        }
    }

    json_text = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json_text;
}

static agnc_status_t agnc_anthropic_parse_response(const char *body, agnc_sse_parser_t *parser)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *content;
    size_t index;
    size_t count;
    char *combined_text = NULL;
    size_t combined_len = 0;
    agnc_status_t status = AGNC_STATUS_OK;

    doc = yyjson_read(body, strlen(body), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    content = yyjson_obj_get(root, "content");
    if (content != NULL && yyjson_is_arr(content)) {
        count = yyjson_arr_size(content);
        for (index = 0; index < count; index++) {
            yyjson_val *block = yyjson_arr_get(content, index);
            yyjson_val *type_val;
            const char *type;

            if (block == NULL || !yyjson_is_obj(block)) {
                continue;
            }

            type_val = yyjson_obj_get(block, "type");
            type = type_val != NULL && yyjson_is_str(type_val) ? yyjson_get_str(type_val) : "";

            if (strcmp(type, "text") == 0) {
                yyjson_val *text_val = yyjson_obj_get(block, "text");
                const char *text = text_val != NULL && yyjson_is_str(text_val) ? yyjson_get_str(text_val) : "";
                size_t text_len = strlen(text);
                char *new_buf = (char *)realloc(combined_text, combined_len + text_len + 1);

                if (new_buf == NULL) {
                    free(combined_text);
                    yyjson_doc_free(doc);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }

                combined_text = new_buf;
                memcpy(combined_text + combined_len, text, text_len);
                combined_len += text_len;
                combined_text[combined_len] = '\0';
            } else if (strcmp(type, "tool_use") == 0) {
                yyjson_val *id_val = yyjson_obj_get(block, "id");
                yyjson_val *name_val = yyjson_obj_get(block, "name");
                yyjson_val *input_val = yyjson_obj_get(block, "input");
                char *input_json = NULL;

                if (input_val != NULL) {
                    input_json = yyjson_val_write(input_val, YYJSON_WRITE_NOFLAG, NULL);
                }
                status = agnc_sse_parser_add_tool_call(
                    parser,
                    id_val != NULL && yyjson_is_str(id_val) ? yyjson_get_str(id_val) : "call",
                    name_val != NULL && yyjson_is_str(name_val) ? yyjson_get_str(name_val) : "tool",
                    input_json != NULL ? input_json : "{}");
                free(input_json);
                if (status != AGNC_STATUS_OK) {
                    free(combined_text);
                    yyjson_doc_free(doc);
                    return status;
                }
            }
        }
    }

    {
        yyjson_val *usage = yyjson_obj_get(root, "usage");
        if (usage != NULL && yyjson_is_obj(usage)) {
            yyjson_val *input = yyjson_obj_get(usage, "input_tokens");
            yyjson_val *output = yyjson_obj_get(usage, "output_tokens");
            if (input != NULL && yyjson_is_num(input)) {
                parser->prompt_tokens = (long)yyjson_get_num(input);
            }
            if (output != NULL && yyjson_is_num(output)) {
                parser->completion_tokens = (long)yyjson_get_num(output);
            }
            if (parser->prompt_tokens >= 0 && parser->completion_tokens >= 0) {
                parser->total_tokens = parser->prompt_tokens + parser->completion_tokens;
            }
        }
    }

    yyjson_doc_free(doc);

    if (combined_text != NULL) {
        agnc_sse_parser_set_assistant_content(parser, combined_text);
        free(combined_text);
    }

    return status;
}

agnc_status_t agnc_anthropic_probe(
    const char *base_url,
    const char *api_key,
    char *detail,
    size_t detail_size)
{
    char *url = NULL;
    char *auth_header = NULL;
    char *request_json =
        "{\"model\":\"claude-3-5-haiku-20241022\",\"max_tokens\":8,\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}]}";
    char *response = NULL;
    char *error_message = NULL;
    agnc_status_t status;

    if (detail != NULL && detail_size > 0) {
        detail[0] = '\0';
    }

    url = agnc_anthropic_join_url(base_url != NULL ? base_url : "https://api.anthropic.com", "/v1/messages");
    if (url == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (api_key != NULL && api_key[0] != '\0') {
        size_t length = strlen("x-api-key: ") + strlen(api_key) + 1;
        auth_header = (char *)malloc(length);
        if (auth_header == NULL) {
            free(url);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        snprintf(auth_header, length, "x-api-key: %s", api_key);
    }

    status = agnc_http_post(url, auth_header, request_json, &response, &error_message, NULL, AGNC_ANTHROPIC_VERSION);
    free(url);
    free(auth_header);

    if (status != AGNC_STATUS_OK) {
        if (detail != NULL && detail_size > 0 && error_message != NULL) {
            snprintf(detail, detail_size, "%s", error_message);
        }
        free(error_message);
        free(response);
        return status;
    }

    if (detail != NULL && detail_size > 0) {
        snprintf(detail, detail_size, "ok");
    }

    free(error_message);
    free(response);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_anthropic_run_turn(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    const agnc_mcp_tool_catalog_t *mcp_catalog,
    agnc_sse_parser_t *parser,
    char **error_message,
    volatile int *cancel_flag)
{
    const agnc_gateway_descriptor_t *gateway;
    char *url = NULL;
    char *auth_header = NULL;
    char *request_json = NULL;
    char *response = NULL;
    agnc_status_t status;

    if (config == NULL || conversation == NULL || parser == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    url = agnc_anthropic_join_url(config->base_url, gateway->endpoint_path != NULL ? gateway->endpoint_path : "/v1/messages");
    if (url == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (config->api_key != NULL && config->api_key[0] != '\0') {
        auth_header = agnc_provider_build_auth_header(gateway, config->api_key);
    } else if (gateway->requires_auth) {
        free(url);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    request_json = agnc_anthropic_build_request_json(config, conversation, mcp_catalog);
    if (request_json == NULL) {
        free(auth_header);
        free(url);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_http_post(url, auth_header, request_json, &response, error_message, cancel_flag, AGNC_ANTHROPIC_VERSION);

    free(request_json);
    free(auth_header);
    free(url);

    if (status != AGNC_STATUS_OK) {
        free(response);
        return status;
    }

    status = agnc_anthropic_parse_response(response, parser);
    free(response);
    return status;
}
