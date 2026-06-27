/*
 * edit_file.c
 *
 * Tool edit file: ganti old_string dengan new_string (satu kejadian pertama).
 * Menolak edit jika old_string tidak ditemukan atau tidak unik.
 */

#include "agnc/atomic_write.h"
#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_EDIT_FILE_MAX_BYTES (512 * 1024)

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_edit_file_parse(
    const char *arguments_json,
    char **path_out,
    char **old_out,
    char **new_out)
{
    yyjson_doc *doc;
    yyjson_val *root;

    if (arguments_json == NULL || path_out == NULL || old_out == NULL || new_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *path_out = NULL;
    *old_out = NULL;
    *new_out = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);

    {
        yyjson_val *path_value = yyjson_obj_get(root, "path");
        yyjson_val *old_value = yyjson_obj_get(root, "old_string");
        yyjson_val *new_value = yyjson_obj_get(root, "new_string");

        if (path_value == NULL || !yyjson_is_str(path_value) ||
            old_value == NULL || !yyjson_is_str(old_value) ||
            new_value == NULL || !yyjson_is_str(new_value)) {
            yyjson_doc_free(doc);
            return AGNC_STATUS_TOOL_FAILED;
        }

        *path_out = agnc_strdup_local(yyjson_get_str(path_value));
        *old_out = agnc_strdup_local(yyjson_get_str(old_value));
        *new_out = agnc_strdup_local(yyjson_get_str(new_value));
    }

    yyjson_doc_free(doc);

    if (*path_out == NULL || *old_out == NULL || *new_out == NULL) {
        free(*path_out);
        free(*old_out);
        free(*new_out);
        *path_out = NULL;
        *old_out = NULL;
        *new_out = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if ((*old_out)[0] == '\0') {
        free(*path_out);
        free(*old_out);
        free(*new_out);
        *path_out = NULL;
        *old_out = NULL;
        *new_out = NULL;
        return AGNC_STATUS_TOOL_FAILED;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_edit_file_read_all(const char *path, char **content_out, size_t *length_out)
{
    FILE *file;
    long file_size;
    char *buffer;
    size_t read_size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    if ((size_t)file_size > AGNC_EDIT_FILE_MAX_BYTES) {
        fclose(file);
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        free(buffer);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer[read_size] = '\0';
    *content_out = buffer;
    *length_out = read_size;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_edit_file_execute(const char *arguments_json, char **result_text)
{
    char *path = NULL;
    char *old_string = NULL;
    char *new_string = NULL;
    char *resolved = NULL;
    char *content = NULL;
    size_t content_len;
    char *match;
    char *second;
    char *updated;
    size_t old_len;
    size_t new_len;
    size_t updated_len;
    agnc_status_t status;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;

    status = agnc_edit_file_parse(arguments_json, &path, &old_string, &new_string);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing path, old_string, or new_string");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_resolve(path, &resolved);
    if (status != AGNC_STATUS_OK) {
        free(path);
        free(old_string);
        free(new_string);
        *result_text = agnc_strdup_local("error: cannot resolve path");
        return status == AGNC_STATUS_TOOL_DENIED ? AGNC_STATUS_TOOL_DENIED : AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_validate_workspace(resolved);
    if (status != AGNC_STATUS_OK) {
        free(path);
        free(old_string);
        free(new_string);
        free(resolved);
        *result_text = agnc_strdup_local("error: path outside workspace");
        return AGNC_STATUS_TOOL_DENIED;
    }

    status = agnc_edit_file_read_all(resolved, &content, &content_len);
    if (status != AGNC_STATUS_OK) {
        free(path);
        free(old_string);
        free(new_string);
        free(resolved);
        *result_text = agnc_strdup_local("error: cannot read file");
        return AGNC_STATUS_TOOL_FAILED;
    }

    match = strstr(content, old_string);
    if (match == NULL) {
        free(path);
        free(old_string);
        free(new_string);
        free(resolved);
        free(content);
        *result_text = agnc_strdup_local("error: old_string not found");
        return AGNC_STATUS_TOOL_FAILED;
    }

    second = strstr(match + strlen(old_string), old_string);
    if (second != NULL) {
        free(path);
        free(old_string);
        free(new_string);
        free(resolved);
        free(content);
        *result_text = agnc_strdup_local("error: old_string is not unique");
        return AGNC_STATUS_TOOL_FAILED;
    }

    old_len = strlen(old_string);
    new_len = strlen(new_string);
    updated_len = content_len - old_len + new_len;

    updated = (char *)malloc(updated_len + 1);
    if (updated == NULL) {
        free(path);
        free(old_string);
        free(new_string);
        free(resolved);
        free(content);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    {
        size_t prefix_len = (size_t)(match - content);
        memcpy(updated, content, prefix_len);
        memcpy(updated + prefix_len, new_string, new_len);
        memcpy(updated + prefix_len + new_len, match + old_len, content_len - prefix_len - old_len);
        updated[updated_len] = '\0';
    }

    status = agnc_atomic_write_file(resolved, updated, updated_len);

    free(path);
    free(old_string);
    free(new_string);
    free(resolved);
    free(content);
    free(updated);

    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: failed to write file");
        return status;
    }

    *result_text = agnc_strdup_local("ok: file edited");
    return AGNC_STATUS_OK;
}

const char *agnc_tool_edit_file_path_preview(const char *arguments_json)
{
    static char preview[512];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *path_value;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    path_value = yyjson_obj_get(root, "path");
    if (path_value != NULL && yyjson_is_str(path_value)) {
        snprintf(preview, sizeof(preview), "%s", yyjson_get_str(path_value));
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "(unknown)");
    return preview;
}
