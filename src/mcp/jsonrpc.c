/*
 * jsonrpc.c
 *
 * JSON-RPC 2.0 minimal untuk protokol MCP (newline-framed di lapisan transport).
 */

#include "agnc/mcp/jsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static char *agnc_jsonrpc_strdup_val(yyjson_val *value)
{
    char *text;

    if (value == NULL) {
        return NULL;
    }

    text = yyjson_val_write(value, 0, NULL);
    return text;
}

static char *agnc_jsonrpc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_jsonrpc_message_init(agnc_jsonrpc_message_t *message)
{
    if (message == NULL) {
        return;
    }

    memset(message, 0, sizeof(*message));
}

void agnc_jsonrpc_message_free(agnc_jsonrpc_message_t *message)
{
    if (message == NULL) {
        return;
    }

    free(message->method);
    free(message->params_json);
    free(message->result_json);
    free(message->error_message);
    agnc_jsonrpc_message_init(message);
}

static agnc_status_t agnc_jsonrpc_validate_version(yyjson_val *root)
{
    yyjson_val *version = yyjson_obj_get(root, "jsonrpc");

    if (version == NULL || !yyjson_is_str(version)) {
        return AGNC_STATUS_JSON_ERROR;
    }

    if (strcmp(yyjson_get_str(version), "2.0") != 0) {
        return AGNC_STATUS_JSON_ERROR;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_jsonrpc_parse_line(const char *json_line, agnc_jsonrpc_message_t *message)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *id_val;
    yyjson_val *method_val;
    yyjson_val *params_val;
    yyjson_val *result_val;
    yyjson_val *error_val;
    agnc_status_t status;

    if (json_line == NULL || message == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_jsonrpc_message_free(message);

    doc = yyjson_read(json_line, strlen(json_line), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    status = agnc_jsonrpc_validate_version(root);
    if (status != AGNC_STATUS_OK) {
        yyjson_doc_free(doc);
        return status;
    }

    id_val = yyjson_obj_get(root, "id");
    if (id_val != NULL) {
        if (yyjson_is_int(id_val)) {
            message->has_id = 1;
            message->id = yyjson_get_int(id_val);
        } else if (yyjson_is_str(id_val)) {
            /* MCP memakai id numerik; string tetap dicatat sebagai 0 + has_id. */
            message->has_id = 1;
            message->id = 0;
        }
    }

    method_val = yyjson_obj_get(root, "method");
    params_val = yyjson_obj_get(root, "params");
    result_val = yyjson_obj_get(root, "result");
    error_val = yyjson_obj_get(root, "error");

    if (method_val != NULL && yyjson_is_str(method_val)) {
        message->method = agnc_jsonrpc_strdup_local(yyjson_get_str(method_val));
        if (message->method == NULL) {
            yyjson_doc_free(doc);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        if (message->has_id) {
            message->is_request = 1;
        } else {
            message->is_notification = 1;
        }

        message->params_json = agnc_jsonrpc_strdup_val(params_val);
        if (params_val != NULL && message->params_json == NULL) {
            yyjson_doc_free(doc);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    } else if (result_val != NULL || error_val != NULL) {
        message->is_response = 1;
        message->result_json = agnc_jsonrpc_strdup_val(result_val);
        if (result_val != NULL && message->result_json == NULL) {
            yyjson_doc_free(doc);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        if (error_val != NULL && yyjson_is_obj(error_val)) {
            yyjson_val *code_val = yyjson_obj_get(error_val, "code");
            yyjson_val *msg_val = yyjson_obj_get(error_val, "message");

            message->has_error = 1;
            if (code_val != NULL && yyjson_is_int(code_val)) {
                message->error_code = (int)yyjson_get_int(code_val);
            }
            if (msg_val != NULL && yyjson_is_str(msg_val)) {
                message->error_message = agnc_jsonrpc_strdup_local(yyjson_get_str(msg_val));
                if (message->error_message == NULL) {
                    yyjson_doc_free(doc);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }
            }
        }
    } else {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    yyjson_doc_free(doc);
    return AGNC_STATUS_OK;
}

/* Sisipkan objek/array JSON immutable ke dokumen mutable (untuk params/result). */
static int agnc_jsonrpc_attach_json_field(
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const char *key,
    const char *json_text)
{
    yyjson_doc *immutable;
    yyjson_val *value;
    yyjson_mut_val *copy;

    if (json_text == NULL || json_text[0] == '\0') {
        yyjson_mut_obj_add_obj(doc, root, key);
        return 1;
    }

    immutable = yyjson_read(json_text, strlen(json_text), 0);
    if (immutable == NULL) {
        return 0;
    }

    value = yyjson_doc_get_root(immutable);
    copy = yyjson_val_mut_copy(doc, value);
    yyjson_doc_free(immutable);

    if (copy == NULL) {
        return 0;
    }

    yyjson_mut_obj_add_val(doc, root, key, copy);
    return 1;
}

static char *agnc_jsonrpc_write_object(
    const char *method,
    int64_t id,
    int include_id,
    const char *params_json,
    const char *result_json,
    int error_code,
    const char *error_message)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *error_obj;
    char *output;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");

    if (include_id) {
        yyjson_mut_obj_add_int(doc, root, "id", id);
    }

    if (method != NULL) {
        yyjson_mut_obj_add_str(doc, root, "method", method);
        if (params_json != NULL && params_json[0] != '\0') {
            if (!agnc_jsonrpc_attach_json_field(doc, root, "params", params_json)) {
                yyjson_mut_doc_free(doc);
                return NULL;
            }
        }
    } else if (error_message != NULL) {
        error_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, error_obj, "code", error_code);
        yyjson_mut_obj_add_str(doc, error_obj, "message", error_message);
        yyjson_mut_obj_add_val(doc, root, "error", error_obj);
    } else if (result_json != NULL) {
        if (!agnc_jsonrpc_attach_json_field(doc, root, "result", result_json)) {
            yyjson_mut_doc_free(doc);
            return NULL;
        }
    } else {
        yyjson_mut_obj_add_obj(doc, root, "result");
    }

    output = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return output;
}

char *agnc_jsonrpc_format_request(int64_t id, const char *method, const char *params_json)
{
    if (method == NULL) {
        return NULL;
    }

    return agnc_jsonrpc_write_object(method, id, 1, params_json, NULL, 0, NULL);
}

char *agnc_jsonrpc_format_notification(const char *method, const char *params_json)
{
    if (method == NULL) {
        return NULL;
    }

    return agnc_jsonrpc_write_object(method, 0, 0, params_json, NULL, 0, NULL);
}

char *agnc_jsonrpc_format_response_ok(int64_t id, const char *result_json)
{
    return agnc_jsonrpc_write_object(NULL, id, 1, NULL, result_json, 0, NULL);
}

char *agnc_jsonrpc_format_response_error(int64_t id, int code, const char *message)
{
    if (message == NULL) {
        message = "error";
    }

    return agnc_jsonrpc_write_object(NULL, id, 1, NULL, NULL, code, message);
}
