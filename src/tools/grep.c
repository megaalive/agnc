/*
 * grep.c
 *
 * Tool grep via ripgrep (rg) — spawn proses eksternal, batasi output.
 */

#include "agnc/rg_locate.h"
#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#ifdef _MSC_VER
#include <process.h>
#endif

#define AGNC_GREP_MAX_OUTPUT (32 * 1024)
#define AGNC_GREP_MAX_MATCHES 100

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_grep_parse(const char *arguments_json, char **pattern_out, char **path_out)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *pattern_value;
    yyjson_val *path_value;

    if (arguments_json == NULL || pattern_out == NULL || path_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *pattern_out = NULL;
    *path_out = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    pattern_value = yyjson_obj_get(root, "pattern");
    path_value = yyjson_obj_get(root, "path");

    if (pattern_value == NULL || !yyjson_is_str(pattern_value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_TOOL_FAILED;
    }

    *pattern_out = agnc_strdup_local(yyjson_get_str(pattern_value));
    if (path_value != NULL && yyjson_is_str(path_value)) {
        *path_out = agnc_strdup_local(yyjson_get_str(path_value));
    } else {
        *path_out = agnc_strdup_local(".");
    }

    yyjson_doc_free(doc);

    if (*pattern_out == NULL || *path_out == NULL) {
        free(*pattern_out);
        free(*path_out);
        *pattern_out = NULL;
        *path_out = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static int agnc_grep_pattern_is_safe(const char *pattern)
{
    return pattern != NULL && strchr(pattern, '"') == NULL;
}

static agnc_status_t agnc_grep_run_rg(const char *pattern, const char *search_path, char **result_text)
{
    const char *rg_binary;
    char *command;
    size_t command_len;
    FILE *pipe;
    char buffer[4096];
    char *output;
    size_t total = 0;
    size_t read_size;
    int truncated = 0;
    int exit_hint = 0;

    rg_binary = agnc_rg_locate_binary();
    if (rg_binary == NULL) {
        *result_text = agnc_strdup_local(
            "error: ripgrep (rg) not found. Install ripgrep or set AGNC_RG_PATH. Do not use shell.");
        return AGNC_STATUS_TOOL_FAILED;
    }

    command_len = strlen(rg_binary) + strlen(pattern) + strlen(search_path) + 256;
    command = (char *)malloc(command_len);
    if (command == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (!agnc_grep_pattern_is_safe(pattern)) {
        free(command);
        *result_text = agnc_strdup_local("error: pattern must not contain double quotes");
        return AGNC_STATUS_TOOL_FAILED;
    }

    /* _popen di Windows lewat cmd.exe; quote pattern dan path agar spasi aman. */
    snprintf(
        command,
        command_len,
        "%s --no-heading --line-number -C 2 --max-count %d \"%s\" \"%s\" 2>&1",
        rg_binary,
        AGNC_GREP_MAX_MATCHES,
        pattern,
        search_path);

#ifdef _WIN32
    pipe = _popen(command, "rt");
#else
    pipe = popen(command, "r");
#endif
    free(command);

    if (pipe == NULL) {
        *result_text = agnc_strdup_local("error: cannot start rg (install ripgrep and ensure rg is in PATH)");
        return AGNC_STATUS_TOOL_FAILED;
    }

    output = (char *)malloc(AGNC_GREP_MAX_OUTPUT + 1);
    if (output == NULL) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    while ((read_size = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        size_t space_left;
        size_t copy_len;

        if (total >= AGNC_GREP_MAX_OUTPUT) {
            truncated = 1;
            break;
        }

        space_left = AGNC_GREP_MAX_OUTPUT - total;
        copy_len = read_size < space_left ? read_size : space_left;
        memcpy(output + total, buffer, copy_len);
        total += copy_len;

        if (copy_len < read_size) {
            truncated = 1;
            break;
        }
    }

    while (fread(buffer, 1, sizeof(buffer), pipe) > 0) {
        truncated = 1;
    }

#ifdef _WIN32
    exit_hint = _pclose(pipe);
#else
    exit_hint = pclose(pipe);
#endif

    output[total] = '\0';

    if (total == 0) {
        free(output);
        *result_text = agnc_strdup_local("(no matches)");
        return AGNC_STATUS_OK;
    }

    /* rg menulis error ke stderr yang kita tangkap; beri tahu model jangan pakai shell. */
    if (exit_hint != 0 && (strstr(output, "IO error") != NULL || strstr(output, "No such file") != NULL ||
                           strstr(output, "cannot find") != NULL)) {
        char *error_msg = (char *)malloc(total + 96);
        if (error_msg != NULL) {
            snprintf(error_msg, total + 96, "error: rg failed (do not use shell findstr): %s", output);
            free(output);
            *result_text = error_msg;
            return AGNC_STATUS_TOOL_FAILED;
        }
    }

    if (truncated) {
        static const char suffix[] = "\n...(output truncated)";
        size_t suffix_len = sizeof(suffix) - 1;
        size_t keep = total;

        if (keep + suffix_len > AGNC_GREP_MAX_OUTPUT) {
            keep = AGNC_GREP_MAX_OUTPUT - suffix_len;
        }
        memcpy(output + keep, suffix, suffix_len + 1);
    }

    {
        static const char header[] =
            "grep results (max "
            "100"
            " matches, 2 lines context; output may truncate at 32KB):\n";
        size_t header_len = sizeof(header) - 1;
        char *formatted;

        formatted = (char *)malloc(header_len + total + 1);
        if (formatted == NULL) {
            *result_text = output;
            return AGNC_STATUS_OK;
        }
        memcpy(formatted, header, header_len);
        memcpy(formatted + header_len, output, total + 1);
        free(output);
        *result_text = formatted;
    }
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_grep_execute(const char *arguments_json, char **result_text)
{
    char *pattern = NULL;
    char *path = NULL;
    char *resolved = NULL;
    agnc_status_t status;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;

    status = agnc_grep_parse(arguments_json, &pattern, &path);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing pattern argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_resolve_search(path, &resolved);
    if (status != AGNC_STATUS_OK) {
        free(pattern);
        free(path);
        *result_text = agnc_strdup_local("error: cannot resolve search path");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_validate_workspace(resolved);
    if (status != AGNC_STATUS_OK) {
        free(pattern);
        free(path);
        free(resolved);
        *result_text = agnc_strdup_local("error: search path outside workspace");
        return AGNC_STATUS_TOOL_DENIED;
    }

    status = agnc_grep_run_rg(pattern, resolved, result_text);

    free(pattern);
    free(path);
    free(resolved);
    return status;
}

const char *agnc_tool_grep_pattern_preview(const char *arguments_json)
{
    static char preview[256];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *pattern_value;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    pattern_value = yyjson_obj_get(root, "pattern");
    if (pattern_value != NULL && yyjson_is_str(pattern_value)) {
        snprintf(preview, sizeof(preview), "%s", yyjson_get_str(pattern_value));
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "(unknown)");
    return preview;
}
