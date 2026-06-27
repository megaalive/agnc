/*
 * write_file.c
 *
 * Tool tulis file teks dengan atomic write dan validasi workspace.
 */

#include "agnc/atomic_write.h"
#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_write_file_parse(
    const char *arguments_json,
    char **path_out,
    char **content_out)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *path_value;
    yyjson_val *content_value;

    if (arguments_json == NULL || path_out == NULL || content_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *path_out = NULL;
    *content_out = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    path_value = yyjson_obj_get(root, "path");
    content_value = yyjson_obj_get(root, "content");

    if (path_value == NULL || !yyjson_is_str(path_value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (content_value == NULL || !yyjson_is_str(content_value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_TOOL_FAILED;
    }

    *path_out = agnc_strdup_local(yyjson_get_str(path_value));
    *content_out = agnc_strdup_local(yyjson_get_str(content_value));
    yyjson_doc_free(doc);

    if (*path_out == NULL || *content_out == NULL) {
        free(*path_out);
        free(*content_out);
        *path_out = NULL;
        *content_out = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_write_file_execute(const char *arguments_json, char **result_text)
{
    char *path = NULL;
    char *content = NULL;
    char *resolved = NULL;
    agnc_status_t status;
    size_t content_len;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;

    status = agnc_write_file_parse(arguments_json, &path, &content);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing path or content argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_resolve(path, &resolved);
    if (status != AGNC_STATUS_OK) {
        free(path);
        free(content);
        *result_text = agnc_strdup_local("error: cannot resolve path");
        return status == AGNC_STATUS_TOOL_DENIED ? AGNC_STATUS_TOOL_DENIED : AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_validate_workspace(resolved);
    if (status != AGNC_STATUS_OK) {
        free(path);
        free(content);
        free(resolved);
        *result_text = agnc_strdup_local("error: path outside workspace");
        return AGNC_STATUS_TOOL_DENIED;
    }

    content_len = strlen(content);
    status = agnc_atomic_write_file(resolved, content, content_len);

    free(path);
    free(content);
    free(resolved);

    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: failed to write file");
        return status;
    }

    *result_text = agnc_strdup_local("ok: file written");
    return AGNC_STATUS_OK;
}

const char *agnc_tool_write_file_path_preview(const char *arguments_json)
{
    static char preview[512];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *path_value;
    const char *path;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    path_value = yyjson_obj_get(root, "path");
    if (path_value != NULL && yyjson_is_str(path_value)) {
        path = yyjson_get_str(path_value);
        snprintf(preview, sizeof(preview), "%s", path);
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "(unknown)");
    return preview;
}
