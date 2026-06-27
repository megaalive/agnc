/*
 * stdio.c
 *
 * Transport MCP stdio: spawn proses child, JSON-RPC per baris di stdin/stdout.
 */

#include "agnc/mcp/stdio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

/* MSVC kadang tidak mendeklarasikan API env block saat WIN32_LEAN_AND_MEAN aktif di TU lain. */
WINBASEAPI LPCH WINAPI GetEnvironmentStringsA(void);
WINBASEAPI BOOL WINAPI FreeEnvironmentStringsA(LPCH lpEnvironmentStrings);
#endif

#define AGNC_MCP_STDIO_LINE_INITIAL 4096
#define AGNC_MCP_STDIO_LINE_MAX (4 * 1024 * 1024)

struct agnc_mcp_stdio_conn {
#ifdef _WIN32
    HANDLE process;
    HANDLE thread;
    HANDLE stdin_write;
    HANDLE stdout_read;
    DWORD process_id;
#endif
    int64_t next_id;
};

#ifdef _WIN32
static char *agnc_mcp_stdio_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_mcp_stdio_resolve_command(const char *command, char **resolved_out)
{
    static const char *extensions[] = {".EXE", ".CMD", ".BAT", ".COM"};
    char search_buffer[MAX_PATH];
    char *file_part = NULL;
    DWORD found;
    size_t index;

    if (command == NULL || resolved_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *resolved_out = NULL;

    if (strchr(command, '\\') != NULL || strchr(command, '/') != NULL || strchr(command, '.') != NULL) {
        *resolved_out = agnc_mcp_stdio_strdup_local(command);
        return *resolved_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < sizeof(extensions) / sizeof(extensions[0]); index++) {
        file_part = NULL;
        found = SearchPathA(NULL, command, extensions[index], MAX_PATH, search_buffer, &file_part);
        if (found > 0 && found < MAX_PATH) {
            *resolved_out = agnc_mcp_stdio_strdup_local(search_buffer);
            return *resolved_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    {
        char where_cmd[128];
        FILE *pipe;
        char line[MAX_PATH];

        snprintf(where_cmd, sizeof(where_cmd), "where %s 2>nul", command);
        pipe = _popen(where_cmd, "rt");
        if (pipe != NULL) {
            if (fgets(line, sizeof(line), pipe) != NULL) {
                size_t length = strlen(line);

                while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
                    line[--length] = '\0';
                }

                if (length > 0) {
                    _pclose(pipe);
                    *resolved_out = agnc_mcp_stdio_strdup_local(line);
                    return *resolved_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
                }
            }
            _pclose(pipe);
        }
    }

    return AGNC_STATUS_IO_ERROR;
}

static int agnc_mcp_stdio_arg_needs_quotes(const char *arg)
{
    const unsigned char *cursor;

    if (arg == NULL || arg[0] == '\0') {
        return 1;
    }

    for (cursor = (const unsigned char *)arg; *cursor != '\0'; cursor++) {
        if (*cursor <= ' ' || *cursor == '"') {
            return 1;
        }
    }

    return 0;
}

static agnc_status_t agnc_mcp_stdio_append_quoted(const char *arg, char **buffer, size_t *length, size_t *capacity)
{
    size_t arg_len;
    size_t needed;
    char *next;
    const unsigned char *cursor;

    if (arg == NULL) {
        arg = "";
    }

    arg_len = strlen(arg);
    needed = *length + arg_len + 3;
    if (agnc_mcp_stdio_arg_needs_quotes(arg)) {
        needed += arg_len;
    }

    if (needed > *capacity) {
        size_t new_capacity = *capacity == 0 ? 256 : *capacity;

        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        next = (char *)realloc(*buffer, new_capacity);
        if (next == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        *buffer = next;
        *capacity = new_capacity;
    }

    if (*length > 0) {
        (*buffer)[(*length)++] = ' ';
    }

    if (agnc_mcp_stdio_arg_needs_quotes(arg)) {
        (*buffer)[(*length)++] = '"';
        for (cursor = (const unsigned char *)arg; *cursor != '\0'; cursor++) {
            if (*cursor == '"') {
                (*buffer)[(*length)++] = '"';
            }
            (*buffer)[(*length)++] = (char)*cursor;
        }
        (*buffer)[(*length)++] = '"';
    } else {
        memcpy(*buffer + *length, arg, arg_len);
        *length += arg_len;
    }

    (*buffer)[*length] = '\0';
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_mcp_stdio_build_command_line(
    const char *command,
    const char *const *argv_extra,
    size_t argc,
    char **cmdline_out)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    agnc_status_t status;
    size_t index;

    if (command == NULL || cmdline_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *cmdline_out = NULL;
    status = agnc_mcp_stdio_append_quoted(command, &buffer, &length, &capacity);
    if (status != AGNC_STATUS_OK) {
        free(buffer);
        return status;
    }

    for (index = 0; index < argc; index++) {
        status = agnc_mcp_stdio_append_quoted(argv_extra[index], &buffer, &length, &capacity);
        if (status != AGNC_STATUS_OK) {
            free(buffer);
            return status;
        }
    }

    *cmdline_out = buffer;
    return AGNC_STATUS_OK;
}

static void agnc_mcp_stdio_close_handles(HANDLE *handles, size_t count)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (handles[index] != NULL && handles[index] != INVALID_HANDLE_VALUE) {
            CloseHandle(handles[index]);
            handles[index] = NULL;
        }
    }
}

typedef struct {
    char **entries;
    size_t count;
    size_t capacity;
} agnc_mcp_env_list_t;

static void agnc_mcp_env_list_clear(agnc_mcp_env_list_t *list)
{
    size_t index;

    if (list == NULL || list->entries == NULL) {
        return;
    }

    for (index = 0; index < list->count; index++) {
        free(list->entries[index]);
    }

    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int agnc_mcp_env_entry_key_equals(const char *entry, const char *key)
{
    const char *equals_sign;
    size_t key_len;

    if (entry == NULL || key == NULL || key[0] == '\0') {
        return 0;
    }

    equals_sign = strchr(entry, '=');
    if (equals_sign == NULL) {
        return 0;
    }

    key_len = strlen(key);
    if ((size_t)(equals_sign - entry) != key_len) {
        return 0;
    }

    return _strnicmp(entry, key, key_len) == 0;
}

static agnc_status_t agnc_mcp_env_list_push(agnc_mcp_env_list_t *list, const char *entry)
{
    char *copy;
    char **next_entries;
    size_t new_capacity;

    if (list == NULL || entry == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    copy = agnc_mcp_stdio_strdup_local(entry);
    if (copy == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (list->count == list->capacity) {
        new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        next_entries = (char **)realloc(list->entries, new_capacity * sizeof(*list->entries));
        if (next_entries == NULL) {
            free(copy);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        list->entries = next_entries;
        list->capacity = new_capacity;
    }

    list->entries[list->count++] = copy;
    return AGNC_STATUS_OK;
}

static void agnc_mcp_env_list_remove_key(agnc_mcp_env_list_t *list, const char *key)
{
    size_t index;

    if (list == NULL || list->entries == NULL || key == NULL) {
        return;
    }

    for (index = 0; index < list->count; index++) {
        if (agnc_mcp_env_entry_key_equals(list->entries[index], key)) {
            free(list->entries[index]);
            if (index + 1 < list->count) {
                memmove(
                    &list->entries[index],
                    &list->entries[index + 1],
                    (list->count - index - 1) * sizeof(*list->entries));
            }
            list->count--;
            return;
        }
    }
}

static agnc_status_t agnc_mcp_stdio_build_merged_env_block(
    const char *const *env_keys,
    const char *const *env_values,
    size_t env_count,
    char **block_out)
{
    LPCH parent_env;
    agnc_mcp_env_list_t list;
    char *block;
    char pair[8192];
    size_t total_size;
    size_t index;
    size_t offset;
    agnc_status_t status;

    if (block_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *block_out = NULL;
    memset(&list, 0, sizeof(list));

    parent_env = GetEnvironmentStringsA();
    if (parent_env == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    for (LPCH cursor = parent_env; *cursor != '\0'; cursor += strlen(cursor) + 1) {
        if (cursor[0] == '=') {
            continue;
        }

        status = agnc_mcp_env_list_push(&list, cursor);
        if (status != AGNC_STATUS_OK) {
            FreeEnvironmentStringsA(parent_env);
            agnc_mcp_env_list_clear(&list);
            return status;
        }
    }

    FreeEnvironmentStringsA(parent_env);

    for (index = 0; index < env_count; index++) {
        if (env_keys[index] == NULL || env_keys[index][0] == '\0') {
            continue;
        }

        agnc_mcp_env_list_remove_key(&list, env_keys[index]);
        snprintf(
            pair,
            sizeof(pair),
            "%s=%s",
            env_keys[index],
            env_values != NULL && env_values[index] != NULL ? env_values[index] : "");
        status = agnc_mcp_env_list_push(&list, pair);
        if (status != AGNC_STATUS_OK) {
            agnc_mcp_env_list_clear(&list);
            return status;
        }
    }

    total_size = 1;
    for (index = 0; index < list.count; index++) {
        total_size += strlen(list.entries[index]) + 1;
    }

    block = (char *)malloc(total_size);
    if (block == NULL) {
        agnc_mcp_env_list_clear(&list);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    offset = 0;
    for (index = 0; index < list.count; index++) {
        size_t entry_len = strlen(list.entries[index]);

        memcpy(block + offset, list.entries[index], entry_len + 1);
        offset += entry_len + 1;
    }

    block[offset] = '\0';
    agnc_mcp_env_list_clear(&list);
    *block_out = block;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_mcp_stdio_read_byte(HANDLE stdout_read, unsigned char *byte_out, unsigned timeout_ms)
{
    DWORD started;
    DWORD available;

    if (byte_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    started = GetTickCount();
    for (;;) {
        if (!PeekNamedPipe(stdout_read, NULL, 0, NULL, &available, NULL)) {
            return AGNC_STATUS_IO_ERROR;
        }

        if (available > 0) {
            DWORD read_count = 0;

            if (!ReadFile(stdout_read, byte_out, 1, &read_count, NULL) || read_count != 1) {
                return AGNC_STATUS_IO_ERROR;
            }

            return AGNC_STATUS_OK;
        }

        if (timeout_ms > 0 && (GetTickCount() - started) >= timeout_ms) {
            return AGNC_STATUS_IO_ERROR;
        }

        Sleep(5);
    }
}
#endif

agnc_status_t agnc_mcp_stdio_spawn(
    const char *command,
    const char *const *argv_extra,
    size_t argc,
    const char *cwd,
    const char *const *env_keys,
    const char *const *env_values,
    size_t env_count,
    agnc_mcp_stdio_conn_t **conn_out)
{
    agnc_mcp_stdio_conn_t *conn;

    if (conn_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *conn_out = NULL;

    if (command == NULL || command[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    conn = (agnc_mcp_stdio_conn_t *)calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES security_attributes;
        HANDLE child_stdin_read = INVALID_HANDLE_VALUE;
        HANDLE child_stdin_write = INVALID_HANDLE_VALUE;
        HANDLE child_stdout_read = INVALID_HANDLE_VALUE;
        HANDLE child_stdout_write = INVALID_HANDLE_VALUE;
        HANDLE child_stderr_null = INVALID_HANDLE_VALUE;
        STARTUPINFOA startup_info;
        PROCESS_INFORMATION process_info;
        char *command_line = NULL;
        char *resolved_command = NULL;
        char *environment_block = NULL;
        char current_directory[MAX_PATH];
        agnc_status_t status;
        BOOL created;

        memset(&security_attributes, 0, sizeof(security_attributes));
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;

        if (!CreatePipe(&child_stdin_read, &child_stdin_write, &security_attributes, 0)) {
            free(conn);
            return AGNC_STATUS_IO_ERROR;
        }

        if (!CreatePipe(&child_stdout_read, &child_stdout_write, &security_attributes, 0)) {
            agnc_mcp_stdio_close_handles(
                (HANDLE[]){child_stdin_read, child_stdin_write, child_stdout_read, child_stdout_write},
                4);
            free(conn);
            return AGNC_STATUS_IO_ERROR;
        }

        if (!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
            agnc_mcp_stdio_close_handles(
                (HANDLE[]){child_stdin_read, child_stdin_write, child_stdout_read, child_stdout_write},
                4);
            free(conn);
            return AGNC_STATUS_IO_ERROR;
        }

        status = agnc_mcp_stdio_resolve_command(command, &resolved_command);
        if (status != AGNC_STATUS_OK) {
            agnc_mcp_stdio_close_handles(
                (HANDLE[]){child_stdin_read, child_stdin_write, child_stdout_read, child_stdout_write},
                4);
            free(conn);
            return status;
        }

        status = agnc_mcp_stdio_build_command_line(resolved_command, argv_extra, argc, &command_line);
        free(resolved_command);
        if (status != AGNC_STATUS_OK) {
            agnc_mcp_stdio_close_handles(
                (HANDLE[]){child_stdin_read, child_stdin_write, child_stdout_read, child_stdout_write},
                4);
            free(conn);
            return status;
        }

        memset(&startup_info, 0, sizeof(startup_info));
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = child_stdin_read;
        startup_info.hStdOutput = child_stdout_write;
        child_stderr_null = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &security_attributes, OPEN_EXISTING, 0, NULL);
        startup_info.hStdError = child_stderr_null != INVALID_HANDLE_VALUE ? child_stderr_null : child_stdout_write;

        memset(&process_info, 0, sizeof(process_info));
        memset(current_directory, 0, sizeof(current_directory));
        if (cwd != NULL && cwd[0] != '\0') {
            snprintf(current_directory, sizeof(current_directory), "%s", cwd);
        }

        if (env_count > 0 && env_keys != NULL) {
            status = agnc_mcp_stdio_build_merged_env_block(env_keys, env_values, env_count, &environment_block);
            if (status != AGNC_STATUS_OK) {
                agnc_mcp_stdio_close_handles(
                    (HANDLE[]){child_stdin_read, child_stdin_write, child_stdout_read, child_stdout_write},
                    4);
                free(conn);
                return status;
            }
        }

        created = CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            environment_block,
            cwd != NULL && cwd[0] != '\0' ? current_directory : NULL,
            &startup_info,
            &process_info);

        free(environment_block);
        free(command_line);
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdout_write);
        if (child_stderr_null != INVALID_HANDLE_VALUE) {
            CloseHandle(child_stderr_null);
        }

        if (!created) {
            CloseHandle(child_stdin_write);
            CloseHandle(child_stdout_read);
            free(conn);
            return AGNC_STATUS_IO_ERROR;
        }

        conn->process = process_info.hProcess;
        conn->thread = process_info.hThread;
        conn->stdin_write = child_stdin_write;
        conn->stdout_read = child_stdout_read;
        conn->process_id = process_info.dwProcessId;
        conn->next_id = 1;
        *conn_out = conn;
        return AGNC_STATUS_OK;
    }
#else
    (void)argv_extra;
    (void)argc;
    (void)cwd;
    (void)env_keys;
    (void)env_values;
    (void)env_count;
    free(conn);
    return AGNC_STATUS_IO_ERROR;
#endif
}

void agnc_mcp_stdio_close(agnc_mcp_stdio_conn_t *conn)
{
    if (conn == NULL) {
        return;
    }

#ifdef _WIN32
    if (conn->stdin_write != NULL && conn->stdin_write != INVALID_HANDLE_VALUE) {
        CloseHandle(conn->stdin_write);
        conn->stdin_write = NULL;
    }

    if (conn->stdout_read != NULL && conn->stdout_read != INVALID_HANDLE_VALUE) {
        CloseHandle(conn->stdout_read);
        conn->stdout_read = NULL;
    }

    if (conn->process != NULL && conn->process != INVALID_HANDLE_VALUE) {
        DWORD wait_result = WaitForSingleObject(conn->process, 2000);

        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(conn->process, 1);
            WaitForSingleObject(conn->process, 5000);
        }

        CloseHandle(conn->process);
        conn->process = NULL;
    }

    if (conn->thread != NULL && conn->thread != INVALID_HANDLE_VALUE) {
        CloseHandle(conn->thread);
        conn->thread = NULL;
    }
#endif

    free(conn);
}

agnc_status_t agnc_mcp_stdio_write_line(agnc_mcp_stdio_conn_t *conn, const char *json_line)
{
    size_t length;
    DWORD written;
    const char newline = '\n';

    if (conn == NULL || json_line == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

#ifdef _WIN32
    length = strlen(json_line);
    if (!WriteFile(conn->stdin_write, json_line, (DWORD)length, &written, NULL) || written != length) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (!WriteFile(conn->stdin_write, &newline, 1, &written, NULL) || written != 1) {
        return AGNC_STATUS_IO_ERROR;
    }

    return AGNC_STATUS_OK;
#else
    (void)length;
    (void)written;
    (void)newline;
    return AGNC_STATUS_IO_ERROR;
#endif
}

agnc_status_t agnc_mcp_stdio_read_message(
    agnc_mcp_stdio_conn_t *conn,
    agnc_jsonrpc_message_t *message,
    unsigned timeout_ms)
{
    char *buffer;
    size_t length;
    size_t capacity;
    agnc_status_t status;

    if (conn == NULL || message == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_jsonrpc_message_init(message);

#ifdef _WIN32
    buffer = (char *)malloc(AGNC_MCP_STDIO_LINE_INITIAL);
    if (buffer == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    length = 0;
    capacity = AGNC_MCP_STDIO_LINE_INITIAL;

    for (;;) {
        unsigned char byte;

        if (length + 1 >= capacity) {
            char *grown;

            if (capacity >= AGNC_MCP_STDIO_LINE_MAX) {
                free(buffer);
                return AGNC_STATUS_IO_ERROR;
            }

            capacity *= 2;
            if (capacity > AGNC_MCP_STDIO_LINE_MAX) {
                capacity = AGNC_MCP_STDIO_LINE_MAX;
            }

            grown = (char *)realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }

            buffer = grown;
        }

        status = agnc_mcp_stdio_read_byte(conn->stdout_read, &byte, timeout_ms);
        if (status != AGNC_STATUS_OK) {
            free(buffer);
            return status;
        }

        if (byte == '\n') {
            break;
        }

        if (byte != '\r') {
            buffer[length++] = (char)byte;
        }
    }

    buffer[length] = '\0';

    while (buffer[0] == '\0' || buffer[0] == ' ' || buffer[0] == '\t') {
        if (buffer[0] == '\0') {
            free(buffer);
            return agnc_mcp_stdio_read_message(conn, message, timeout_ms);
        }

        memmove(buffer, buffer + 1, length);
        length--;
        buffer[length] = '\0';
    }

    status = agnc_jsonrpc_parse_line(buffer, message);
    free(buffer);
    if (status == AGNC_STATUS_JSON_ERROR) {
        return agnc_mcp_stdio_read_message(conn, message, timeout_ms);
    }

    return status;
#else
    (void)timeout_ms;
    return AGNC_STATUS_IO_ERROR;
#endif
}

agnc_status_t agnc_mcp_stdio_call(
    agnc_mcp_stdio_conn_t *conn,
    const char *method,
    const char *params_json,
    agnc_jsonrpc_message_t *response,
    unsigned timeout_ms)
{
    int64_t request_id;
    char *request_json;
    agnc_status_t status;
    agnc_jsonrpc_message_t message;

    if (conn == NULL || method == NULL || response == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_jsonrpc_message_init(response);
    request_id = conn->next_id++;

    request_json = agnc_jsonrpc_format_request(request_id, method, params_json);
    if (request_json == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_mcp_stdio_write_line(conn, request_json);
    free(request_json);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    for (;;) {
        agnc_jsonrpc_message_init(&message);
        status = agnc_mcp_stdio_read_message(conn, &message, timeout_ms);
        if (status != AGNC_STATUS_OK) {
            agnc_jsonrpc_message_free(&message);
            return status;
        }

        if (message.is_response && message.has_id && message.id == request_id) {
            *response = message;
            return AGNC_STATUS_OK;
        }

        agnc_jsonrpc_message_free(&message);
    }
}

agnc_status_t agnc_mcp_stdio_notify(
    agnc_mcp_stdio_conn_t *conn,
    const char *method,
    const char *params_json)
{
    char *notification_json;
    agnc_status_t status;

    if (conn == NULL || method == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    notification_json = agnc_jsonrpc_format_notification(method, params_json);
    if (notification_json == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_mcp_stdio_write_line(conn, notification_json);
    free(notification_json);
    return status;
}
