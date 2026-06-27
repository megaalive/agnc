/*
 * client.c
 *
 * Client MCP minimal: initialize handshake dan tools/list.
 */

#include "agnc/mcp/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_MCP_CLIENT_VERSION "0.1.0"

static char *agnc_mcp_client_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_mcp_client_strdup_result_field(const char *result_json, const char *field)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *field_value;
    char *output;

    if (result_json == NULL || field == NULL) {
        return NULL;
    }

    doc = yyjson_read(result_json, strlen(result_json), 0);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_doc_get_root(doc);
    field_value = yyjson_obj_get(root, field);
    if (field_value == NULL) {
        yyjson_doc_free(doc);
        return NULL;
    }

    output = yyjson_val_write(field_value, 0, NULL);
    yyjson_doc_free(doc);
    return output;
}

void agnc_mcp_client_init(agnc_mcp_client_t *client)
{
    if (client == NULL) {
        return;
    }

    memset(client, 0, sizeof(*client));
}

void agnc_mcp_client_close(agnc_mcp_client_t *client)
{
    if (client == NULL) {
        return;
    }

    agnc_mcp_stdio_close(client->transport);
    client->transport = NULL;
    client->initialized = 0;
}

agnc_status_t agnc_mcp_client_connect(
    const char *command,
    const char *const *argv_extra,
    size_t argc,
    const char *cwd,
    const char *const *env_keys,
    const char *const *env_values,
    size_t env_count,
    agnc_mcp_client_t *client,
    char **tools_json_out,
    unsigned timeout_ms)
{
    agnc_jsonrpc_message_t initialize_response;
    agnc_jsonrpc_message_t tools_response;
    agnc_status_t status;
    const char *initialize_params =
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"agnc\",\"version\":\"" AGNC_MCP_CLIENT_VERSION "\"}}";

    if (client == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (tools_json_out != NULL) {
        *tools_json_out = NULL;
    }

    agnc_mcp_client_init(client);

    status = agnc_mcp_stdio_spawn(
        command,
        argv_extra,
        argc,
        cwd,
        env_keys,
        env_values,
        env_count,
        &client->transport);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    agnc_jsonrpc_message_init(&initialize_response);
    status = agnc_mcp_stdio_call(
        client->transport,
        "initialize",
        initialize_params,
        &initialize_response,
        timeout_ms);
    if (status != AGNC_STATUS_OK) {
        agnc_jsonrpc_message_free(&initialize_response);
        agnc_mcp_client_close(client);
        return status;
    }

    if (initialize_response.has_error) {
        agnc_jsonrpc_message_free(&initialize_response);
        agnc_mcp_client_close(client);
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    agnc_jsonrpc_message_free(&initialize_response);

    status = agnc_mcp_stdio_notify(client->transport, "notifications/initialized", NULL);
    if (status != AGNC_STATUS_OK) {
        agnc_mcp_client_close(client);
        return status;
    }

    client->initialized = 1;

    if (tools_json_out == NULL) {
        return AGNC_STATUS_OK;
    }

    agnc_jsonrpc_message_init(&tools_response);
    status = agnc_mcp_stdio_call(client->transport, "tools/list", "{}", &tools_response, timeout_ms);
    if (status != AGNC_STATUS_OK) {
        agnc_jsonrpc_message_free(&tools_response);
        agnc_mcp_client_close(client);
        return status;
    }

    if (tools_response.has_error) {
        agnc_jsonrpc_message_free(&tools_response);
        agnc_mcp_client_close(client);
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    *tools_json_out = agnc_mcp_client_strdup_result_field(tools_response.result_json, "tools");
    agnc_jsonrpc_message_free(&tools_response);

    if (*tools_json_out == NULL) {
        agnc_mcp_client_close(client);
        return AGNC_STATUS_JSON_ERROR;
    }

    return AGNC_STATUS_OK;
}

static int agnc_mcp_client_attach_json_field(
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

static char *agnc_mcp_client_format_call_result(const char *result_json)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *content;
    yyjson_val *is_error_value;
    size_t index;
    size_t count;
    char *output;
    size_t output_len = 0;
    size_t output_cap = 0;
    char *buffer = NULL;
    int is_error = 0;

    if (result_json == NULL) {
        return agnc_mcp_client_strdup_local("(no result)");
    }

    doc = yyjson_read(result_json, strlen(result_json), 0);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_doc_get_root(doc);
    is_error_value = yyjson_obj_get(root, "isError");
    if (is_error_value != NULL && yyjson_is_bool(is_error_value)) {
        is_error = yyjson_get_bool(is_error_value) ? 1 : 0;
    }

    content = yyjson_obj_get(root, "content");
    if (content == NULL || !yyjson_is_arr(content)) {
        output = yyjson_val_write(root, 0, NULL);
        yyjson_doc_free(doc);
        return output;
    }

    count = yyjson_arr_size(content);
    for (index = 0; index < count; index++) {
        yyjson_val *item = yyjson_arr_get(content, index);
        yyjson_val *type_value;
        yyjson_val *text_value;
        const char *text;
        size_t text_len;

        if (item == NULL || !yyjson_is_obj(item)) {
            continue;
        }

        type_value = yyjson_obj_get(item, "type");
        text_value = yyjson_obj_get(item, "text");
        if (type_value == NULL || !yyjson_is_str(type_value) ||
            strcmp(yyjson_get_str(type_value), "text") != 0 || text_value == NULL || !yyjson_is_str(text_value)) {
            continue;
        }

        text = yyjson_get_str(text_value);
        text_len = strlen(text);
        if (output_len + text_len + 2 > output_cap) {
            char *grown;
            size_t new_cap = output_cap == 0 ? 256 : output_cap * 2;

            while (new_cap < output_len + text_len + 2) {
                new_cap *= 2;
            }

            grown = (char *)realloc(buffer, new_cap);
            if (grown == NULL) {
                free(buffer);
                yyjson_doc_free(doc);
                return NULL;
            }

            buffer = grown;
            output_cap = new_cap;
        }

        if (output_len > 0) {
            buffer[output_len++] = '\n';
        }

        memcpy(buffer + output_len, text, text_len);
        output_len += text_len;
    }

    yyjson_doc_free(doc);

    if (buffer == NULL) {
        return agnc_mcp_client_strdup_result_field(result_json, NULL);
    }

    buffer[output_len] = '\0';

    if (!is_error) {
        return buffer;
    }

    output_len += strlen("error: ");
    output = (char *)malloc(output_len + 1);
    if (output == NULL) {
        free(buffer);
        return NULL;
    }

    snprintf(output, output_len + 1, "error: %s", buffer);
    free(buffer);
    return output;
}

agnc_status_t agnc_mcp_client_call_tool(
    agnc_mcp_client_t *client,
    const char *tool_name,
    const char *arguments_json,
    char **result_text_out,
    unsigned timeout_ms)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    char *params_json;
    agnc_jsonrpc_message_t response;
    agnc_status_t status;
    char *formatted;

    if (client == NULL || tool_name == NULL || result_text_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!client->initialized || client->transport == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text_out = NULL;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "name", tool_name);

    if (arguments_json != NULL && arguments_json[0] != '\0') {
        if (!agnc_mcp_client_attach_json_field(doc, root, "arguments", arguments_json)) {
            yyjson_mut_obj_add_obj(doc, root, "arguments");
        }
    } else {
        yyjson_mut_obj_add_obj(doc, root, "arguments");
    }

    params_json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    if (params_json == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    agnc_jsonrpc_message_init(&response);
    status = agnc_mcp_stdio_call(client->transport, "tools/call", params_json, &response, timeout_ms);
    free(params_json);
    if (status != AGNC_STATUS_OK) {
        agnc_jsonrpc_message_free(&response);
        return status;
    }

    if (response.has_error) {
        if (response.error_message != NULL) {
            size_t length = strlen(response.error_message) + 8;
            *result_text_out = (char *)malloc(length);
            if (*result_text_out != NULL) {
                snprintf(*result_text_out, length, "error: %s", response.error_message);
            }
        } else {
            *result_text_out = agnc_mcp_client_strdup_local("error: mcp tool call failed");
        }
        agnc_jsonrpc_message_free(&response);
        return AGNC_STATUS_TOOL_FAILED;
    }

    formatted = agnc_mcp_client_format_call_result(response.result_json);
    agnc_jsonrpc_message_free(&response);
    if (formatted == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    *result_text_out = formatted;
    return AGNC_STATUS_OK;
}
