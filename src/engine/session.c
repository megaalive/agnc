/*
 * session.c
 *
 * Persistensi riwayat chat ke ~/.agnc/sessions/<nama>.json.
 * Pointer sesi aktif di active.txt; simpan memakai atomic write.
 */

#include "agnc/session.h"

#include "agnc/atomic_write.h"
#include "agnc/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#endif

#include <yyjson.h>

#define AGNC_SESSION_DEFAULT_NAME "current"
#define AGNC_SESSION_NAME_MAX 48

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

/* File temp atomic write: <nama>.json.tmp atau <nama>.json.tmp.<pid>. */
static int agnc_session_is_stale_temp_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    return strstr(name, ".json.tmp") != NULL;
}

#ifdef _WIN32
static int agnc_session_is_active_process_temp(const char *name)
{
    char suffix[32];
    size_t name_len;
    size_t suffix_len;

    snprintf(suffix, sizeof(suffix), ".tmp.%lu", (unsigned long)GetCurrentProcessId());
    suffix_len = strlen(suffix);
    name_len = strlen(name);
    if (name_len < suffix_len) {
        return 0;
    }
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}
#endif

agnc_status_t agnc_session_cleanup_stale_temp_files_in_dir(const char *sessions_dir)
{
    if (sessions_dir == NULL || sessions_dir[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!agnc_path_exists(sessions_dir)) {
        return AGNC_STATUS_OK;
    }

#ifdef _WIN32
    {
        char pattern[1024];
        WIN32_FIND_DATAA data;
        HANDLE handle;

        snprintf(pattern, sizeof(pattern), "%s\\*.json.tmp*", sessions_dir);
        handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return AGNC_STATUS_OK;
        }

        do {
            char full_path[1024];

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (agnc_session_is_active_process_temp(data.cFileName)) {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s\\%s", sessions_dir, data.cFileName);
            (void)remove(full_path);
        } while (FindNextFileA(handle, &data));

        FindClose(handle);
    }
#else
    {
        DIR *dir = opendir(sessions_dir);

        if (dir == NULL) {
            return AGNC_STATUS_OK;
        }

        for (;;) {
            struct dirent *entry = readdir(dir);
            char full_path[1024];

            if (entry == NULL) {
                break;
            }
            if (!agnc_session_is_stale_temp_name(entry->d_name)) {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", sessions_dir, entry->d_name);
            (void)remove(full_path);
        }

        closedir(dir);
    }
#endif

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_cleanup_stale_temp_files(void)
{
    char *dir = NULL;
    agnc_status_t status;

    status = agnc_session_default_dir(&dir);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_cleanup_stale_temp_files_in_dir(dir);
    free(dir);
    return status;
}

agnc_status_t agnc_session_default_dir(char **output)
{
    char *home_sessions;

    if (output == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (agnc_path_expand_user("~/.agnc/sessions", &home_sessions) != AGNC_STATUS_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    *output = home_sessions;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_ensure_dirs(void)
{
    char *dir = NULL;
    char *parent = NULL;
    agnc_status_t status;

    status = agnc_session_default_dir(&dir);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_path_expand_user("~/.agnc", &parent);
    if (status != AGNC_STATUS_OK) {
        free(dir);
        return status;
    }

#ifdef _WIN32
    if (!agnc_path_exists(parent)) {
        if (_mkdir(parent) != 0 && errno != EEXIST) {
            free(parent);
            free(dir);
            return AGNC_STATUS_IO_ERROR;
        }
    }
    if (!agnc_path_exists(dir)) {
        if (_mkdir(dir) != 0 && errno != EEXIST) {
            free(parent);
            free(dir);
            return AGNC_STATUS_IO_ERROR;
        }
    }
#else
    if (!agnc_path_exists(parent)) {
        if (mkdir(parent, 0700) != 0 && errno != EEXIST) {
            free(parent);
            free(dir);
            return AGNC_STATUS_IO_ERROR;
        }
    }
    if (!agnc_path_exists(dir)) {
        if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
            free(parent);
            free(dir);
            return AGNC_STATUS_IO_ERROR;
        }
    }
#endif

    free(parent);
    free(dir);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_validate_name(const char *name)
{
    size_t length;
    size_t index;

    if (name == NULL || name[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    length = strlen(name);
    if (length > AGNC_SESSION_NAME_MAX) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0; index < length; index++) {
        char ch = name[index];

        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-') {
            continue;
        }

        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_path_for_name(const char *name, char **output)
{
    char *dir;
    char *path;
    const char *session_name;
    agnc_status_t status;

    if (output == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *output = NULL;
    session_name = (name == NULL || name[0] == '\0') ? AGNC_SESSION_DEFAULT_NAME : name;
    status = agnc_session_validate_name(session_name);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_ensure_dirs();
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_default_dir(&dir);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    path = (char *)malloc(strlen(dir) + strlen(session_name) + 16);
    if (path == NULL) {
        free(dir);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

#ifdef _WIN32
    snprintf(path, strlen(dir) + strlen(session_name) + 16, "%s\\%s.json", dir, session_name);
#else
    snprintf(path, strlen(dir) + strlen(session_name) + 16, "%s/%s.json", dir, session_name);
#endif

    free(dir);
    *output = path;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_active_name_load(char **name_out)
{
    char *dir;
    char *path;
    FILE *file;
    char buffer[AGNC_SESSION_NAME_MAX + 4];
    agnc_status_t status;

    if (name_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *name_out = NULL;
    status = agnc_session_ensure_dirs();
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_default_dir(&dir);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

#ifdef _WIN32
    path = (char *)malloc(strlen(dir) + 32);
    if (path == NULL) {
        free(dir);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    snprintf(path, strlen(dir) + 32, "%s\\active.txt", dir);
#else
    path = (char *)malloc(strlen(dir) + 32);
    if (path == NULL) {
        free(dir);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    snprintf(path, strlen(dir) + 32, "%s/active.txt", dir);
#endif
    free(dir);

    if (!agnc_path_exists(path)) {
        *name_out = agnc_strdup_local(AGNC_SESSION_DEFAULT_NAME);
        free(path);
        return *name_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    file = fopen(path, "rb");
    free(path);
    if (file == NULL) {
        *name_out = agnc_strdup_local(AGNC_SESSION_DEFAULT_NAME);
        return *name_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
        fclose(file);
        *name_out = agnc_strdup_local(AGNC_SESSION_DEFAULT_NAME);
        return *name_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }
    fclose(file);

    {
        size_t length = strlen(buffer);
        while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r' ||
                              buffer[length - 1] == ' ' || buffer[length - 1] == '\t')) {
            buffer[--length] = '\0';
        }
    }

    if (buffer[0] == '\0' || agnc_session_validate_name(buffer) != AGNC_STATUS_OK) {
        *name_out = agnc_strdup_local(AGNC_SESSION_DEFAULT_NAME);
    } else {
        *name_out = agnc_strdup_local(buffer);
    }

    return *name_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
}

agnc_status_t agnc_session_active_name_save(const char *name)
{
    char *dir;
    char *path;
    agnc_status_t status;

    status = agnc_session_validate_name(name);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_ensure_dirs();
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_default_dir(&dir);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

#ifdef _WIN32
    path = (char *)malloc(strlen(dir) + 32);
    if (path == NULL) {
        free(dir);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    snprintf(path, strlen(dir) + 32, "%s\\active.txt", dir);
#else
    path = (char *)malloc(strlen(dir) + 32);
    if (path == NULL) {
        free(dir);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    snprintf(path, strlen(dir) + 32, "%s/active.txt", dir);
#endif
    free(dir);

    status = agnc_atomic_write_file(path, name, strlen(name));
    free(path);
    return status;
}

static int agnc_session_name_compare(const void *left, const void *right)
{
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;

    if (a == NULL && b == NULL) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }

    return strcmp(a, b);
}

agnc_status_t agnc_session_list_names(const char *sessions_dir, char ***names_out, size_t *count_out)
{
    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;

    if (sessions_dir == NULL || names_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *names_out = NULL;
    *count_out = 0;

    if (!agnc_path_exists(sessions_dir)) {
        return AGNC_STATUS_OK;
    }

#ifdef _WIN32
    {
        char pattern[1024];
        WIN32_FIND_DATAA data;
        HANDLE handle;

        snprintf(pattern, sizeof(pattern), "%s\\*.json", sessions_dir);
        handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return AGNC_STATUS_OK;
        }

        do {
            const char *dot;
            size_t base_len;
            char *session_name;

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (agnc_session_is_stale_temp_name(data.cFileName)) {
                continue;
            }

            dot = strrchr(data.cFileName, '.');
            if (dot == NULL || strcmp(dot, ".json") != 0) {
                continue;
            }

            base_len = (size_t)(dot - data.cFileName);
            if (base_len == 0 || base_len > AGNC_SESSION_NAME_MAX) {
                continue;
            }

            session_name = (char *)malloc(base_len + 1);
            if (session_name == NULL) {
                agnc_session_list_names_free(names, count);
                FindClose(handle);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }

            memcpy(session_name, data.cFileName, base_len);
            session_name[base_len] = '\0';

            if (count == capacity) {
                char **grown;
                size_t new_capacity = capacity == 0 ? 8 : capacity * 2;

                grown = (char **)realloc(names, new_capacity * sizeof(*names));
                if (grown == NULL) {
                    free(session_name);
                    agnc_session_list_names_free(names, count);
                    FindClose(handle);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }

                names = grown;
                capacity = new_capacity;
            }

            names[count++] = session_name;
        } while (FindNextFileA(handle, &data));

        FindClose(handle);
    }
#else
    {
        DIR *dir = opendir(sessions_dir);

        if (dir == NULL) {
            return AGNC_STATUS_OK;
        }

        for (;;) {
            struct dirent *entry = readdir(dir);
            const char *dot;
            size_t base_len;
            char *session_name;

            if (entry == NULL) {
                break;
            }
            if (agnc_session_is_stale_temp_name(entry->d_name)) {
                continue;
            }

            dot = strrchr(entry->d_name, '.');
            if (dot == NULL || strcmp(dot, ".json") != 0) {
                continue;
            }

            base_len = (size_t)(dot - entry->d_name);
            if (base_len == 0 || base_len > AGNC_SESSION_NAME_MAX) {
                continue;
            }

            session_name = (char *)malloc(base_len + 1);
            if (session_name == NULL) {
                agnc_session_list_names_free(names, count);
                closedir(dir);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }

            memcpy(session_name, entry->d_name, base_len);
            session_name[base_len] = '\0';

            if (count == capacity) {
                char **grown;
                size_t new_capacity = capacity == 0 ? 8 : capacity * 2;

                grown = (char **)realloc(names, new_capacity * sizeof(*names));
                if (grown == NULL) {
                    free(session_name);
                    agnc_session_list_names_free(names, count);
                    closedir(dir);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }

                names = grown;
                capacity = new_capacity;
            }

            names[count++] = session_name;
        }

        closedir(dir);
    }
#endif

    if (count > 1) {
        qsort(names, count, sizeof(*names), agnc_session_name_compare);
    }

    *names_out = names;
    *count_out = count;
    return AGNC_STATUS_OK;
}

void agnc_session_list_names_free(char **names, size_t count)
{
    size_t index;

    if (names == NULL) {
        return;
    }

    for (index = 0; index < count; index++) {
        free(names[index]);
    }

    free(names);
}

agnc_status_t agnc_session_current_path(char **output)
{
    return agnc_session_path_for_name(AGNC_SESSION_DEFAULT_NAME, output);
}

agnc_status_t agnc_session_load(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out)
{
    FILE *file;
    long size;
    char *text;
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *messages;
    size_t index;
    agnc_status_t status = AGNC_STATUS_OK;

    if (path == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (provider_id_out != NULL) {
        *provider_id_out = NULL;
    }
    if (model_out != NULL) {
        *model_out = NULL;
    }

    if (!agnc_path_exists(path)) {
        return AGNC_STATUS_IO_ERROR;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }
    text[size] = '\0';
    fclose(file);

    doc = yyjson_read(text, (size_t)size, 0);
    free(text);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    messages = yyjson_obj_get(root, "messages");
    if (messages != NULL && yyjson_is_arr(messages)) {
        agnc_conversation_clear(conversation);
        for (index = 0; index < yyjson_arr_size(messages); index++) {
            yyjson_val *entry = yyjson_arr_get(messages, index);
            yyjson_val *role = yyjson_obj_get(entry, "role");
            yyjson_val *content = yyjson_obj_get(entry, "content");
            yyjson_val *tool_call_id = yyjson_obj_get(entry, "tool_call_id");
            yyjson_val *tool_name = yyjson_obj_get(entry, "tool_name");
            yyjson_val *tool_arguments = yyjson_obj_get(entry, "tool_arguments");

            if (role == NULL || !yyjson_is_str(role)) {
                continue;
            }

            status = agnc_conversation_push(
                conversation,
                yyjson_get_str(role),
                content != NULL && yyjson_is_str(content) ? yyjson_get_str(content) : NULL,
                tool_call_id != NULL && yyjson_is_str(tool_call_id) ? yyjson_get_str(tool_call_id) : NULL,
                tool_name != NULL && yyjson_is_str(tool_name) ? yyjson_get_str(tool_name) : NULL,
                tool_arguments != NULL && yyjson_is_str(tool_arguments) ? yyjson_get_str(tool_arguments) : NULL);
            if (status != AGNC_STATUS_OK) {
                break;
            }
        }
    }

    if (status == AGNC_STATUS_OK && provider_id_out != NULL) {
        yyjson_val *provider_id = yyjson_obj_get(root, "provider_id");
        if (provider_id != NULL && yyjson_is_str(provider_id)) {
            *provider_id_out = agnc_strdup_local(yyjson_get_str(provider_id));
        }
    }

    if (status == AGNC_STATUS_OK && model_out != NULL) {
        yyjson_val *model = yyjson_obj_get(root, "model");
        if (model != NULL && yyjson_is_str(model)) {
            *model_out = agnc_strdup_local(yyjson_get_str(model));
        }
    }

    yyjson_doc_free(doc);
    return status;
}

agnc_status_t agnc_session_save(
    const char *path,
    const agnc_conversation_t *conversation,
    const agnc_config_t *config)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *messages;
    char *json_text;
    size_t index;
    agnc_status_t status;
    time_t now;

    if (path == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    time(&now);
    yyjson_mut_obj_add_int(doc, root, "saved_at", (int64_t)now);

    if (config != NULL) {
        if (config->provider_id != NULL) {
            yyjson_mut_obj_add_str(doc, root, "provider_id", config->provider_id);
        }
        if (config->model != NULL) {
            yyjson_mut_obj_add_str(doc, root, "model", config->model);
        }
        if (config->gateway_id != NULL) {
            yyjson_mut_obj_add_str(doc, root, "gateway_id", config->gateway_id);
        }
    }

    messages = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "messages", messages);

    for (index = 0; index < conversation->count; index++) {
        const agnc_conversation_message_t *item = &conversation->items[index];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, entry, "role", item->role);
        if (item->content != NULL) {
            yyjson_mut_obj_add_str(doc, entry, "content", item->content);
        }
        if (item->tool_call_id != NULL) {
            yyjson_mut_obj_add_str(doc, entry, "tool_call_id", item->tool_call_id);
        }
        if (item->tool_name != NULL) {
            yyjson_mut_obj_add_str(doc, entry, "tool_name", item->tool_name);
        }
        if (item->tool_arguments != NULL) {
            yyjson_mut_obj_add_str(doc, entry, "tool_arguments", item->tool_arguments);
        }
        yyjson_mut_arr_append(messages, entry);
    }

    json_text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(doc);
    if (json_text == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_atomic_write_file(path, json_text, strlen(json_text));
    free(json_text);
    return status;
}
