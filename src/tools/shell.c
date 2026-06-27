/*
 * shell.c
 *
 * Tool shell Fase 1: jalankan perintah via PowerShell di Windows.
 * Output dibatasi ukuran agar tidak membanjiri konteks model.
 */

#include "agnc/tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#ifdef _MSC_VER
#include <process.h>
#endif

#define AGNC_SHELL_MAX_OUTPUT (64 * 1024)

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_extract_command_from_jsonish(const char *text)
{
    const char *cursor;
    const char *end;
    size_t length;
    char *command;
    char *best;

    if (text == NULL) {
        return NULL;
    }

    best = NULL;
    cursor = text;
    while ((cursor = strstr(cursor, "\"command\"")) != NULL) {
        cursor += 9;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ':') {
            cursor++;
        }
        if (*cursor != '"') {
            continue;
        }
        cursor++;
        end = cursor;
        while (*end != '\0' && *end != '"') {
            if (*end == '\\' && *(end + 1) != '\0') {
                end += 2;
            } else {
                end++;
            }
        }
        if (*end != '"') {
            continue;
        }

        length = (size_t)(end - cursor);
        command = (char *)malloc(length + 1);
        if (command == NULL) {
            free(best);
            return NULL;
        }
        memcpy(command, cursor, length);
        command[length] = '\0';

        free(best);
        best = command;
        cursor = end + 1;
    }

    return best;
}

static char *agnc_shell_unescape_json_string(const char *escaped)
{
    size_t length;
    size_t index;
    size_t out_index;
    char *result;

    if (escaped == NULL) {
        return NULL;
    }

    length = strlen(escaped);
    result = (char *)malloc(length + 1);
    if (result == NULL) {
        return NULL;
    }

    out_index = 0;
    for (index = 0; index < length; index++) {
        if (escaped[index] == '\\' && index + 1 < length) {
            index++;
            switch (escaped[index]) {
            case 'n':
                result[out_index++] = '\n';
                break;
            case 't':
                result[out_index++] = '\t';
                break;
            case '"':
            case '\\':
                result[out_index++] = escaped[index];
                break;
            default:
                result[out_index++] = '\\';
                result[out_index++] = escaped[index];
                break;
            }
        } else {
            result[out_index++] = escaped[index];
        }
    }

    result[out_index] = '\0';
    return result;
}

static agnc_status_t agnc_shell_parse_command(const char *arguments_json, char **command_out)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *command_value;
    char *owned_command;
    const char *command;

    if (arguments_json == NULL || command_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *command_out = NULL;
    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc != NULL) {
        root = yyjson_doc_get_root(doc);
        command_value = yyjson_obj_get(root, "command");
        if (command_value != NULL && yyjson_is_str(command_value)) {
            command = yyjson_get_str(command_value);
            if (command[0] != '\0') {
                *command_out = agnc_strdup_local(command);
                yyjson_doc_free(doc);
                return *command_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
            }
        }
        yyjson_doc_free(doc);
    }

    owned_command = agnc_extract_command_from_jsonish(arguments_json);
    if (owned_command != NULL) {
        *command_out = agnc_shell_unescape_json_string(owned_command);
        free(owned_command);
        return *command_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_TOOL_FAILED;
}

#ifdef _WIN32
static agnc_status_t agnc_shell_run_powershell(const char *command, char **result_text)
{
    char *shell_line;
    size_t shell_line_len;
    FILE *pipe;
    char buffer[4096];
    size_t total = 0;
    char *output;
    size_t read_size;
    int truncated = 0;

    shell_line_len = strlen(command) + 64;
    shell_line = (char *)malloc(shell_line_len);
    if (shell_line == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    snprintf(
        shell_line,
        shell_line_len,
        "powershell.exe -NoProfile -NonInteractive -Command \"%s\" 2>&1",
        command);

    pipe = _popen(shell_line, "rt");
    free(shell_line);
    if (pipe == NULL) {
        *result_text = agnc_strdup_local("error: cannot start shell");
        return AGNC_STATUS_TOOL_FAILED;
    }

    output = (char *)malloc(AGNC_SHELL_MAX_OUTPUT + 1);
    if (output == NULL) {
        _pclose(pipe);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    /* Batasi output; wajib NUL-terminate agar strdup di agent loop aman. */
    while ((read_size = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        size_t space_left;
        size_t copy_len;

        if (total >= AGNC_SHELL_MAX_OUTPUT) {
            truncated = 1;
            break;
        }

        space_left = AGNC_SHELL_MAX_OUTPUT - total;
        copy_len = read_size < space_left ? read_size : space_left;
        memcpy(output + total, buffer, copy_len);
        total += copy_len;

        if (copy_len < read_size) {
            truncated = 1;
            break;
        }
    }

    /* Buang sisa pipe agar proses child tidak hang saat _pclose. */
    while (fread(buffer, 1, sizeof(buffer), pipe) > 0) {
        truncated = 1;
    }

    _pclose(pipe);
    output[total] = '\0';

    if (total == 0) {
        free(output);
        *result_text = agnc_strdup_local("(no output)");
        return AGNC_STATUS_OK;
    }

    if (truncated) {
        static const char suffix[] = "\n...(output truncated)";
        size_t suffix_len = sizeof(suffix) - 1;
        size_t keep = total;

        if (keep + suffix_len > AGNC_SHELL_MAX_OUTPUT) {
            keep = AGNC_SHELL_MAX_OUTPUT - suffix_len;
        }

        memcpy(output + keep, suffix, suffix_len + 1);
        total = keep + suffix_len;
    }

    *result_text = output;
    return AGNC_STATUS_OK;
}
#else
static agnc_status_t agnc_shell_run_powershell(const char *command, char **result_text)
{
    (void)command;
    *result_text = agnc_strdup_local("error: shell tool not implemented on this platform yet");
    return AGNC_STATUS_TOOL_FAILED;
}
#endif

static void agnc_shell_copy_lower(const char *command, char *lower, size_t lower_size)
{
    size_t index;

    if (command == NULL || lower == NULL || lower_size == 0) {
        if (lower != NULL && lower_size > 0) {
            lower[0] = '\0';
        }
        return;
    }

    for (index = 0; command[index] != '\0' && index + 1 < lower_size; index++) {
        char ch = command[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        lower[index] = ch;
    }
    lower[index] = '\0';
}

int agnc_tool_shell_is_search_command(const char *command)
{
    char lower[1024];

    if (command == NULL || command[0] == '\0') {
        return 0;
    }

    agnc_shell_copy_lower(command, lower, sizeof(lower));

    if (strstr(lower, "findstr") != NULL) {
        return 1;
    }
    if (strncmp(lower, "find ", 5) == 0) {
        return 1;
    }
    if (strncmp(lower, "where", 5) == 0) {
        return 1;
    }
    if (strstr(lower, "select-string") != NULL) {
        return 1;
    }
    if (strncmp(lower, "grep ", 5) == 0 || strstr(lower, " grep") != NULL) {
        return 1;
    }
    if (strncmp(lower, "rg ", 3) == 0 || strncmp(lower, "rg.", 3) == 0 || strstr(lower, " rg") != NULL ||
        strstr(lower, "|rg") != NULL || strstr(lower, "| rg") != NULL) {
        return 1;
    }
    if (strstr(lower, "dir /s") != NULL || strstr(lower, "dir /b /s") != NULL) {
        return 1;
    }
    if (strncmp(lower, "dir ", 4) == 0 && (strstr(lower, "src") != NULL || strstr(lower, "*") != NULL)) {
        return 1;
    }
    if (strstr(lower, "get-childitem") != NULL && strstr(lower, "-recurse") != NULL) {
        return 1;
    }
    if (strstr(lower, "gci ") != NULL && strstr(lower, "-r") != NULL) {
        return 1;
    }

    return 0;
}

agnc_status_t agnc_tool_shell_extract_command(const char *arguments_json, char **command_out)
{
    if (command_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *command_out = NULL;
    return agnc_shell_parse_command(arguments_json, command_out);
}

agnc_status_t agnc_tool_shell_execute(const char *arguments_json, char **result_text)
{
    char *command = NULL;
    agnc_status_t status;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;
    status = agnc_shell_parse_command(arguments_json, &command);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing command argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (agnc_tool_shell_is_search_command(command)) {
        free(command);
        *result_text = agnc_strdup_local(
            "error: shell search blocked; use the grep tool for text search or glob for file patterns. "
            "Do not retry with findstr, find, or recursive dir.");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_shell_run_powershell(command, result_text);
    free(command);
    return status;
}

const char *agnc_tool_shell_command_preview(const char *arguments_json)
{
    static char preview[256];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *command_value;
    const char *command;
    size_t length;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    command_value = yyjson_obj_get(root, "command");
    if (command_value != NULL && yyjson_is_str(command_value)) {
        command = yyjson_get_str(command_value);
        length = strlen(command);
        if (length >= sizeof(preview)) {
            memcpy(preview, command, sizeof(preview) - 4);
            preview[sizeof(preview) - 4] = '.';
            preview[sizeof(preview) - 3] = '.';
            preview[sizeof(preview) - 2] = '.';
            preview[sizeof(preview) - 1] = '\0';
        } else {
            snprintf(preview, sizeof(preview), "%s", command);
        }
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "%.200s", arguments_json);
    return preview;
}
