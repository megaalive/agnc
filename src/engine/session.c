/*
 * session.c
 *
 * Persistensi riwayat chat ke ~/.agnc/sessions/<nama>.sqlite (1 sesi = 1 DB).
 * Pointer sesi aktif di active.txt; migrasi otomatis dari .json legacy.
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

#include <sqlite3.h>
#include <yyjson.h>

#define AGNC_SESSION_DEFAULT_NAME "current"
#define AGNC_SESSION_NAME_MAX 48
#define AGNC_SESSION_FILE_EXT ".sqlite"
#define AGNC_SESSION_LEGACY_EXT ".json"

#define AGNC_SESSION_SCHEMA \
    "PRAGMA journal_mode=DELETE;" \
    "CREATE TABLE IF NOT EXISTS meta (" \
    "  key TEXT PRIMARY KEY NOT NULL," \
    "  value TEXT" \
    ");" \
    "CREATE TABLE IF NOT EXISTS messages (" \
    "  id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "  role TEXT NOT NULL," \
    "  content TEXT," \
    "  tool_call_id TEXT," \
    "  tool_name TEXT," \
    "  tool_arguments TEXT" \
    ");"

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

/* File temp: <nama>.sqlite.tmp atau legacy <nama>.json.tmp*. */
static int agnc_session_is_stale_temp_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    return strstr(name, ".sqlite.tmp") != NULL || strstr(name, ".json.tmp") != NULL;
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

        snprintf(pattern, sizeof(pattern), "%s\\*.sqlite.tmp*", sessions_dir);
        handle = FindFirstFileA(pattern, &data);
        if (handle != INVALID_HANDLE_VALUE) {
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
    snprintf(path, strlen(dir) + strlen(session_name) + 16, "%s\\%s%s", dir, session_name, AGNC_SESSION_FILE_EXT);
#else
    snprintf(path, strlen(dir) + strlen(session_name) + 16, "%s/%s%s", dir, session_name, AGNC_SESSION_FILE_EXT);
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

static int agnc_session_list_has_name(char **names, size_t count, const char *session_name)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (names[index] != NULL && strcmp(names[index], session_name) == 0) {
            return 1;
        }
    }

    return 0;
}

static agnc_status_t agnc_session_list_append_unique(
    char ***names,
    size_t *count,
    size_t *capacity,
    const char *session_name)
{
    char *copy;
    char **grown;

    if (names == NULL || count == NULL || capacity == NULL || session_name == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (agnc_session_validate_name(session_name) != AGNC_STATUS_OK) {
        return AGNC_STATUS_OK;
    }

    if (agnc_session_list_has_name(*names, *count, session_name)) {
        return AGNC_STATUS_OK;
    }

    copy = agnc_strdup_local(session_name);
    if (copy == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;

        grown = (char **)realloc(*names, new_capacity * sizeof(**names));
        if (grown == NULL) {
            free(copy);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        *names = grown;
        *capacity = new_capacity;
    }

    (*names)[(*count)++] = copy;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_legacy_json_path(const char *sqlite_path, char **json_out)
{
    size_t length;
    char *json_path;

    if (sqlite_path == NULL || json_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *json_out = NULL;
    length = strlen(sqlite_path);
    if (length < strlen(AGNC_SESSION_FILE_EXT) ||
        strcmp(sqlite_path + length - strlen(AGNC_SESSION_FILE_EXT), AGNC_SESSION_FILE_EXT) != 0) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    json_path = (char *)malloc(length + 8);
    if (json_path == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    memcpy(json_path, sqlite_path, length - strlen(AGNC_SESSION_FILE_EXT));
    json_path[length - strlen(AGNC_SESSION_FILE_EXT)] = '\0';
    strcat(json_path, AGNC_SESSION_LEGACY_EXT);
    *json_out = json_path;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_sqlite_exec_simple(sqlite3 *db, const char *sql)
{
    char *error_message = NULL;
    int rc;

    rc = sqlite3_exec(db, sql, NULL, NULL, &error_message);
    if (rc != SQLITE_OK) {
        if (error_message != NULL) {
            sqlite3_free(error_message);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_sqlite_bind_text_or_null(sqlite3_stmt *stmt, int index, const char *value)
{
    if (value != NULL) {
        return sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT) == SQLITE_OK ? AGNC_STATUS_OK
                                                                                          : AGNC_STATUS_IO_ERROR;
    }

    return sqlite3_bind_null(stmt, index) == SQLITE_OK ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
}

static agnc_status_t agnc_session_sqlite_meta_get(sqlite3 *db, const char *key, char **value_out)
{
    sqlite3_stmt *stmt = NULL;
    agnc_status_t status = AGNC_STATUS_OK;
    int rc;

    if (value_out != NULL) {
        *value_out = NULL;
    }

    rc = sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = ? LIMIT 1", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return AGNC_STATUS_IO_ERROR;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW && value_out != NULL) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text != NULL) {
            *value_out = agnc_strdup_local((const char *)text);
            if (*value_out == NULL) {
                status = AGNC_STATUS_OUT_OF_MEMORY;
            }
        }
    }

    sqlite3_finalize(stmt);
    return status;
}

static agnc_status_t agnc_session_sqlite_meta_set(sqlite3 *db, const char *key, const char *value)
{
    sqlite3_stmt *stmt = NULL;
    agnc_status_t status;
    int rc;

    rc = sqlite3_prepare_v2(
        db,
        "INSERT INTO meta(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
        -1,
        &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_bind_text_or_null(stmt, 2, value);
    if (status != AGNC_STATUS_OK) {
        sqlite3_finalize(stmt);
        return status;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
}

static agnc_status_t agnc_session_load_json_file(
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

            status = agnc_conversation_push_hydrated(
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

static agnc_status_t agnc_session_write_sqlite_file(
    const char *path,
    const agnc_conversation_t *conversation,
    const agnc_config_t *config)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *insert_stmt = NULL;
    char saved_at[32];
    size_t index;
    agnc_status_t status;
    int rc;
    time_t now;

    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        (void)remove(path);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        (void)remove(path);
        return AGNC_STATUS_IO_ERROR;
    }

    if (agnc_session_sqlite_exec_simple(db, "DELETE FROM messages") != AGNC_STATUS_OK ||
        agnc_session_sqlite_exec_simple(db, "DELETE FROM meta") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        (void)remove(path);
        return AGNC_STATUS_IO_ERROR;
    }

    time(&now);
    snprintf(saved_at, sizeof(saved_at), "%lld", (long long)now);
    if (agnc_session_sqlite_meta_set(db, "saved_at", saved_at) != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        (void)remove(path);
        return AGNC_STATUS_IO_ERROR;
    }

    if (config != NULL) {
        if (config->provider_id != NULL &&
            agnc_session_sqlite_meta_set(db, "provider_id", config->provider_id) != AGNC_STATUS_OK) {
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            (void)remove(path);
            return AGNC_STATUS_IO_ERROR;
        }
        if (config->model != NULL && agnc_session_sqlite_meta_set(db, "model", config->model) != AGNC_STATUS_OK) {
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            (void)remove(path);
            return AGNC_STATUS_IO_ERROR;
        }
        if (config->gateway_id != NULL &&
            agnc_session_sqlite_meta_set(db, "gateway_id", config->gateway_id) != AGNC_STATUS_OK) {
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            (void)remove(path);
            return AGNC_STATUS_IO_ERROR;
        }
    }

    rc = sqlite3_prepare_v2(
        db,
        "INSERT INTO messages(role, content, tool_call_id, tool_name, tool_arguments) "
        "VALUES(?, ?, ?, ?, ?)",
        -1,
        &insert_stmt,
        NULL);
    if (rc != SQLITE_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        (void)remove(path);
        return AGNC_STATUS_IO_ERROR;
    }

    for (index = 0; index < conversation->count; index++) {
        const agnc_conversation_message_t *item = agnc_conversation_at(conversation, index);
        if (item == NULL) {
            continue;
        }

        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);

        if (item->role == NULL || sqlite3_bind_text(insert_stmt, 1, item->role, -1, SQLITE_STATIC) != SQLITE_OK) {
            status = AGNC_STATUS_IO_ERROR;
            break;
        }

        status = agnc_session_sqlite_bind_text_or_null(insert_stmt, 2, item->content);
        if (status == AGNC_STATUS_OK) {
            status = agnc_session_sqlite_bind_text_or_null(insert_stmt, 3, item->tool_call_id);
        }
        if (status == AGNC_STATUS_OK) {
            status = agnc_session_sqlite_bind_text_or_null(insert_stmt, 4, item->tool_name);
        }
        if (status == AGNC_STATUS_OK) {
            status = agnc_session_sqlite_bind_text_or_null(insert_stmt, 5, item->tool_arguments);
        }
        if (status != AGNC_STATUS_OK) {
            break;
        }

        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            status = AGNC_STATUS_IO_ERROR;
            break;
        }
    }

    sqlite3_finalize(insert_stmt);

    if (status != AGNC_STATUS_OK ||
        agnc_session_sqlite_exec_simple(db, "COMMIT") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        (void)remove(path);
        return AGNC_STATUS_IO_ERROR;
    }

    sqlite3_close(db);
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_sqlite_message_count(sqlite3 *db, size_t *count_out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *count_out = 0;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM messages", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *count_out = (size_t)sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
}

static agnc_status_t agnc_session_sqlite_update_meta(
    sqlite3 *db,
    const agnc_config_t *config,
    const agnc_conversation_t *conversation)
{
    char saved_at[32];
    time_t now;

    time(&now);
    snprintf(saved_at, sizeof(saved_at), "%lld", (long long)now);
    if (agnc_session_sqlite_meta_set(db, "saved_at", saved_at) != AGNC_STATUS_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (conversation != NULL && conversation->history_summary != NULL) {
        if (agnc_session_sqlite_meta_set(db, "history_summary", conversation->history_summary) != AGNC_STATUS_OK) {
            return AGNC_STATUS_IO_ERROR;
        }
    }

    if (conversation != NULL && conversation->memory_skipped > 0) {
        char skipped[32];
        snprintf(skipped, sizeof(skipped), "%zu", conversation->memory_skipped);
        if (agnc_session_sqlite_meta_set(db, "memory_skipped", skipped) != AGNC_STATUS_OK) {
            return AGNC_STATUS_IO_ERROR;
        }
    }

    if (config != NULL) {
        if (config->provider_id != NULL &&
            agnc_session_sqlite_meta_set(db, "provider_id", config->provider_id) != AGNC_STATUS_OK) {
            return AGNC_STATUS_IO_ERROR;
        }
        if (config->model != NULL && agnc_session_sqlite_meta_set(db, "model", config->model) != AGNC_STATUS_OK) {
            return AGNC_STATUS_IO_ERROR;
        }
        if (config->gateway_id != NULL &&
            agnc_session_sqlite_meta_set(db, "gateway_id", config->gateway_id) != AGNC_STATUS_OK) {
            return AGNC_STATUS_IO_ERROR;
        }
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_session_sqlite_append_message(sqlite3 *db, const agnc_conversation_message_t *item)
{
    sqlite3_stmt *stmt = NULL;
    agnc_status_t status;
    int rc;

    if (item == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_prepare_v2(
        db,
        "INSERT INTO messages(role, content, tool_call_id, tool_name, tool_arguments) "
        "VALUES(?, ?, ?, ?, ?)",
        -1,
        &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (item->role == NULL || sqlite3_bind_text(stmt, 1, item->role, -1, SQLITE_STATIC) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_bind_text_or_null(stmt, 2, item->content);
    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_bind_text_or_null(stmt, 3, item->tool_call_id);
    }
    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_bind_text_or_null(stmt, 4, item->tool_name);
    }
    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_bind_text_or_null(stmt, 5, item->tool_arguments);
    }
    if (status != AGNC_STATUS_OK) {
        sqlite3_finalize(stmt);
        return status;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
}

static agnc_status_t agnc_session_load_sqlite_tail(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out,
    size_t tail_limit)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    size_t total = 0;
    size_t offset = 0;
    agnc_status_t status = AGNC_STATUS_OK;
    int rc;

    if (tail_limit == 0) {
        tail_limit = AGNC_CONVERSATION_MEMORY_LIMIT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_message_count(db, &total);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    agnc_conversation_clear(conversation);
    conversation->db_total = total;
    conversation->unsynced_count = 0;

    if (total > tail_limit) {
        offset = total - tail_limit;
        conversation->memory_skipped = offset;
    } else {
        conversation->memory_skipped = 0;
    }

    if (provider_id_out != NULL) {
        status = agnc_session_sqlite_meta_get(db, "provider_id", provider_id_out);
    }
    if (status == AGNC_STATUS_OK && model_out != NULL) {
        status = agnc_session_sqlite_meta_get(db, "model", model_out);
    }
    if (status == AGNC_STATUS_OK) {
        char *summary = NULL;
        if (agnc_session_sqlite_meta_get(db, "history_summary", &summary) == AGNC_STATUS_OK) {
            conversation->history_summary = summary;
        }
    }

    if (status != AGNC_STATUS_OK || total == 0) {
        sqlite3_close(db);
        return status;
    }

    rc = sqlite3_prepare_v2(
        db,
        "SELECT role, content, tool_call_id, tool_name, tool_arguments "
        "FROM messages ORDER BY id ASC LIMIT -1 OFFSET ?",
        -1,
        &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)offset) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *role = (const char *)sqlite3_column_text(stmt, 0);
        const char *content = (const char *)sqlite3_column_text(stmt, 1);
        const char *tool_call_id = (const char *)sqlite3_column_text(stmt, 2);
        const char *tool_name = (const char *)sqlite3_column_text(stmt, 3);
        const char *tool_arguments = (const char *)sqlite3_column_text(stmt, 4);

        if (role == NULL) {
            continue;
        }

        status = agnc_conversation_push_hydrated(
            conversation, role, content, tool_call_id, tool_name, tool_arguments);
        if (status != AGNC_STATUS_OK) {
            break;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return status;
}

static agnc_status_t agnc_session_load_sqlite_file(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out)
{
    return agnc_session_load_sqlite_tail(
        path, conversation, provider_id_out, model_out, AGNC_CONVERSATION_MEMORY_LIMIT);
}

static agnc_status_t agnc_session_migrate_json_file(const char *json_path, const char *sqlite_path)
{
    agnc_conversation_t conversation;
    agnc_config_t config;
    char *provider_id = NULL;
    char *model = NULL;
    char *gateway_id = NULL;
    agnc_status_t status;

    agnc_conversation_init(&conversation);
    agnc_config_init(&config);

    status = agnc_session_load_json_file(json_path, &conversation, &provider_id, &model);
    if (status != AGNC_STATUS_OK) {
        agnc_conversation_clear(&conversation);
        agnc_config_free(&config);
        return status;
    }

    config.provider_id = provider_id;
    config.model = model;

    {
        FILE *file = fopen(json_path, "rb");
        if (file != NULL) {
            long size;
            char *text;
            yyjson_doc *doc;
            yyjson_val *root;
            yyjson_val *gateway;

            if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0) {
                text = (char *)malloc((size_t)size + 1);
                if (text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size) {
                    text[size] = '\0';
                    doc = yyjson_read(text, (size_t)size, 0);
                    free(text);
                    if (doc != NULL) {
                        root = yyjson_doc_get_root(doc);
                        gateway = yyjson_obj_get(root, "gateway_id");
                        if (gateway != NULL && yyjson_is_str(gateway)) {
                            gateway_id = agnc_strdup_local(yyjson_get_str(gateway));
                            config.gateway_id = gateway_id;
                        }
                        yyjson_doc_free(doc);
                    }
                } else {
                    free(text);
                }
            }
            fclose(file);
        }
    }

    status = agnc_session_write_sqlite_file(sqlite_path, &conversation, &config);
    if (status == AGNC_STATUS_OK) {
        (void)remove(json_path);
    }

    free(gateway_id);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    return status;
}

static agnc_status_t agnc_session_collect_names_with_ext(
    const char *sessions_dir,
    const char *extension,
    int skip_if_sqlite_exists,
    char ***names,
    size_t *count,
    size_t *capacity)
{
#ifdef _WIN32
    char pattern[1024];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    snprintf(pattern, sizeof(pattern), "%s\\*%s", sessions_dir, extension);
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return AGNC_STATUS_OK;
    }

    do {
        const char *dot;
        size_t base_len;
        char session_name[AGNC_SESSION_NAME_MAX + 1];
        char sqlite_path[1024];
        agnc_status_t status;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (agnc_session_is_stale_temp_name(data.cFileName)) {
            continue;
        }

        dot = strrchr(data.cFileName, '.');
        if (dot == NULL || strcmp(dot, extension) != 0) {
            continue;
        }

        base_len = (size_t)(dot - data.cFileName);
        if (base_len == 0 || base_len > AGNC_SESSION_NAME_MAX) {
            continue;
        }

        memcpy(session_name, data.cFileName, base_len);
        session_name[base_len] = '\0';

        if (skip_if_sqlite_exists) {
            snprintf(sqlite_path, sizeof(sqlite_path), "%s\\%s%s", sessions_dir, session_name, AGNC_SESSION_FILE_EXT);
            if (agnc_path_exists(sqlite_path)) {
                continue;
            }
        }

        status = agnc_session_list_append_unique(names, count, capacity, session_name);
        if (status != AGNC_STATUS_OK) {
            FindClose(handle);
            return status;
        }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
#else
    DIR *dir = opendir(sessions_dir);

    if (dir == NULL) {
        return AGNC_STATUS_OK;
    }

    for (;;) {
        struct dirent *entry = readdir(dir);
        const char *dot;
        size_t base_len;
        char session_name[AGNC_SESSION_NAME_MAX + 1];
        char sqlite_path[1024];
        agnc_status_t status;

        if (entry == NULL) {
            break;
        }
        if (agnc_session_is_stale_temp_name(entry->d_name)) {
            continue;
        }

        dot = strrchr(entry->d_name, '.');
        if (dot == NULL || strcmp(dot, extension) != 0) {
            continue;
        }

        base_len = (size_t)(dot - entry->d_name);
        if (base_len == 0 || base_len > AGNC_SESSION_NAME_MAX) {
            continue;
        }

        memcpy(session_name, entry->d_name, base_len);
        session_name[base_len] = '\0';

        if (skip_if_sqlite_exists) {
            snprintf(sqlite_path, sizeof(sqlite_path), "%s/%s%s", sessions_dir, session_name, AGNC_SESSION_FILE_EXT);
            if (agnc_path_exists(sqlite_path)) {
                continue;
            }
        }

        status = agnc_session_list_append_unique(names, count, capacity, session_name);
        if (status != AGNC_STATUS_OK) {
            closedir(dir);
            return status;
        }
    }

    closedir(dir);
#endif

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_list_names(const char *sessions_dir, char ***names_out, size_t *count_out)
{
    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    agnc_status_t status;

    if (sessions_dir == NULL || names_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *names_out = NULL;
    *count_out = 0;

    if (!agnc_path_exists(sessions_dir)) {
        return AGNC_STATUS_OK;
    }

    status = agnc_session_collect_names_with_ext(
        sessions_dir, AGNC_SESSION_FILE_EXT, 0, &names, &count, &capacity);
    if (status != AGNC_STATUS_OK) {
        agnc_session_list_names_free(names, count);
        return status;
    }

    status = agnc_session_collect_names_with_ext(
        sessions_dir, AGNC_SESSION_LEGACY_EXT, 1, &names, &count, &capacity);
    if (status != AGNC_STATUS_OK) {
        agnc_session_list_names_free(names, count);
        return status;
    }

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

agnc_status_t agnc_session_delete_by_name(const char *name)
{
    char *path = NULL;
    char *json_path = NULL;
    agnc_status_t status;
    int existed = 0;

    status = agnc_session_path_for_name(name, &path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (agnc_path_exists(path)) {
        existed = 1;
        if (remove(path) != 0) {
            free(path);
            return AGNC_STATUS_IO_ERROR;
        }
    }

    if (agnc_session_legacy_json_path(path, &json_path) == AGNC_STATUS_OK && agnc_path_exists(json_path)) {
        existed = 1;
        (void)remove(json_path);
    }

    free(json_path);
    free(path);
    return existed ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
}

agnc_status_t agnc_session_load(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out)
{
    char *json_path = NULL;
    agnc_status_t status;

    if (path == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (provider_id_out != NULL) {
        *provider_id_out = NULL;
    }
    if (model_out != NULL) {
        *model_out = NULL;
    }

    if (agnc_path_exists(path)) {
        return agnc_session_load_sqlite_file(path, conversation, provider_id_out, model_out);
    }

    status = agnc_session_legacy_json_path(path, &json_path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (!agnc_path_exists(json_path)) {
        free(json_path);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_migrate_json_file(json_path, path);
    free(json_path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    return agnc_session_load_sqlite_file(path, conversation, provider_id_out, model_out);
}

agnc_status_t agnc_session_sync(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config)
{
    sqlite3 *db = NULL;
    size_t index;
    size_t append_start;
    agnc_status_t status;
    int rc;
    int created = 0;

    if (path == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (conversation->unsynced_count == 0) {
        if (!agnc_path_exists(path)) {
            if (config == NULL) {
                return AGNC_STATUS_OK;
            }
        } else {
            rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
            if (rc != SQLITE_OK || db == NULL) {
                if (db != NULL) {
                    sqlite3_close(db);
                }
                return AGNC_STATUS_IO_ERROR;
            }

            if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
                sqlite3_close(db);
                return AGNC_STATUS_IO_ERROR;
            }

            status = agnc_session_sqlite_update_meta(db, config, conversation);
            if (status == AGNC_STATUS_OK) {
                status = agnc_session_sqlite_exec_simple(db, "COMMIT");
            } else {
                (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            }

            sqlite3_close(db);
            return status;
        }
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    if (!agnc_path_exists(path)) {
        created = 1;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    append_start = conversation->count - conversation->unsynced_count;
    for (index = append_start; index < conversation->count; index++) {
        const agnc_conversation_message_t *item = agnc_conversation_at(conversation, index);

        status = agnc_session_sqlite_append_message(db, item);
        if (status != AGNC_STATUS_OK) {
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            return status;
        }
    }

    status = agnc_session_sqlite_update_meta(db, config, conversation);
    if (status != AGNC_STATUS_OK ||
        agnc_session_sqlite_exec_simple(db, "COMMIT") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    if (agnc_session_sqlite_message_count(db, &conversation->db_total) != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    conversation->unsynced_count = 0;
    sqlite3_close(db);
    (void)created;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_clear_messages(const char *path, const agnc_config_t *config)
{
    sqlite3 *db = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    if (agnc_session_sqlite_exec_simple(db, "DELETE FROM messages") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    if (agnc_session_sqlite_meta_set(db, "usage_prompt_total", "0") != AGNC_STATUS_OK ||
        agnc_session_sqlite_meta_set(db, "usage_completion_total", "0") != AGNC_STATUS_OK ||
        agnc_session_sqlite_meta_set(db, "usage_total", "0") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_update_meta(db, config, NULL);
    if (status != AGNC_STATUS_OK ||
        agnc_session_sqlite_exec_simple(db, "COMMIT") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    sqlite3_close(db);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_compact_storage(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config,
    size_t keep_tail_messages)
{
    sqlite3 *db = NULL;
    char summary[160];
    char *provider_id = NULL;
    char *model = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!agnc_path_exists(path)) {
        return agnc_session_sync(path, conversation, config);
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    {
        sqlite3_stmt *del_stmt = NULL;
        size_t remaining = 0;

        rc = sqlite3_prepare_v2(
            db,
            "DELETE FROM messages WHERE id NOT IN ("
            "  SELECT id FROM messages ORDER BY id DESC LIMIT ?"
            ")",
            -1,
            &del_stmt,
            NULL);
        if (rc != SQLITE_OK) {
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            return AGNC_STATUS_IO_ERROR;
        }

        if (sqlite3_bind_int64(del_stmt, 1, (sqlite3_int64)keep_tail_messages) != SQLITE_OK ||
            sqlite3_step(del_stmt) != SQLITE_DONE) {
            sqlite3_finalize(del_stmt);
            (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
            sqlite3_close(db);
            return AGNC_STATUS_IO_ERROR;
        }
        sqlite3_finalize(del_stmt);

        if (agnc_session_sqlite_message_count(db, &remaining) == AGNC_STATUS_OK) {
            snprintf(
                summary,
                sizeof(summary),
                "Riwayat diringkas; %zu pesan terakhir dipertahankan di storage.",
                remaining);
        } else {
            snprintf(summary, sizeof(summary), "Riwayat diringkas.");
        }

        free(conversation->history_summary);
        conversation->history_summary = agnc_strdup_local(summary);
        if (conversation->history_summary != NULL) {
            (void)agnc_session_sqlite_meta_set(db, "history_summary", conversation->history_summary);
        }
    }

    status = agnc_session_sqlite_update_meta(db, config, conversation);
    if (status != AGNC_STATUS_OK ||
        agnc_session_sqlite_exec_simple(db, "COMMIT") != AGNC_STATUS_OK) {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    sqlite3_close(db);

    status = agnc_session_load_sqlite_tail(path, conversation, &provider_id, &model, AGNC_CONVERSATION_MEMORY_LIMIT);
    free(provider_id);
    free(model);
    return status;
}

agnc_status_t agnc_session_save(
    const char *path,
    const agnc_conversation_t *conversation,
    const agnc_config_t *config)
{
    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    return agnc_session_sync(path, (agnc_conversation_t *)conversation, config);
}

void agnc_session_usage_init(agnc_session_usage_t *usage)
{
    if (usage == NULL) {
        return;
    }

    usage->prompt_tokens = 0;
    usage->completion_tokens = 0;
    usage->total_tokens = 0;
}

static long agnc_session_parse_meta_long(const char *text, long default_value)
{
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0') {
        return default_value;
    }

    value = strtol(text, &end, 10);
    if (end == text) {
        return default_value;
    }

    return value;
}

static agnc_status_t agnc_session_usage_load_db(sqlite3 *db, agnc_session_usage_t *usage_out)
{
    char *prompt_text = NULL;
    char *completion_text = NULL;
    char *total_text = NULL;

    if (db == NULL || usage_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_session_usage_init(usage_out);
    (void)agnc_session_sqlite_meta_get(db, "usage_prompt_total", &prompt_text);
    (void)agnc_session_sqlite_meta_get(db, "usage_completion_total", &completion_text);
    (void)agnc_session_sqlite_meta_get(db, "usage_total", &total_text);

    usage_out->prompt_tokens = agnc_session_parse_meta_long(prompt_text, 0);
    usage_out->completion_tokens = agnc_session_parse_meta_long(completion_text, 0);
    usage_out->total_tokens = agnc_session_parse_meta_long(total_text, 0);

    free(prompt_text);
    free(completion_text);
    free(total_text);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_usage_load(const char *path, agnc_session_usage_t *usage_out)
{
    sqlite3 *db = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || usage_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!agnc_path_exists(path)) {
        agnc_session_usage_init(usage_out);
        return AGNC_STATUS_OK;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_usage_load_db(db, usage_out);
    sqlite3_close(db);
    return status;
}

static agnc_status_t agnc_session_usage_save_db(sqlite3 *db, const agnc_session_usage_t *usage)
{
    char prompt_buf[32];
    char completion_buf[32];
    char total_buf[32];

    if (db == NULL || usage == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    snprintf(prompt_buf, sizeof(prompt_buf), "%ld", usage->prompt_tokens);
    snprintf(completion_buf, sizeof(completion_buf), "%ld", usage->completion_tokens);
    snprintf(total_buf, sizeof(total_buf), "%ld", usage->total_tokens);

    if (agnc_session_sqlite_meta_set(db, "usage_prompt_total", prompt_buf) != AGNC_STATUS_OK ||
        agnc_session_sqlite_meta_set(db, "usage_completion_total", completion_buf) != AGNC_STATUS_OK ||
        agnc_session_sqlite_meta_set(db, "usage_total", total_buf) != AGNC_STATUS_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_usage_save(const char *path, const agnc_session_usage_t *usage)
{
    sqlite3 *db = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || usage == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_usage_save_db(db, usage);
    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_exec_simple(db, "COMMIT");
    } else {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
    }

    sqlite3_close(db);
    return status;
}

agnc_status_t agnc_session_usage_accumulate(
    const char *path,
    long prompt_delta,
    long completion_delta,
    long total_delta)
{
    agnc_session_usage_t usage;
    agnc_status_t status;

    if (path == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (prompt_delta < 0 && completion_delta < 0 && total_delta < 0) {
        return AGNC_STATUS_OK;
    }

    status = agnc_session_usage_load(path, &usage);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (prompt_delta >= 0) {
        usage.prompt_tokens += prompt_delta;
    }
    if (completion_delta >= 0) {
        usage.completion_tokens += completion_delta;
    }
    if (total_delta >= 0) {
        usage.total_tokens += total_delta;
    } else if (prompt_delta >= 0 && completion_delta >= 0) {
        usage.total_tokens += prompt_delta + completion_delta;
    }

    return agnc_session_usage_save(path, &usage);
}

agnc_status_t agnc_session_usage_reset(const char *path)
{
    agnc_session_usage_t usage;

    agnc_session_usage_init(&usage);
    return agnc_session_usage_save(path, &usage);
}

agnc_status_t agnc_session_meta_get(const char *path, const char *key, char **value_out)
{
    sqlite3 *db = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || key == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    status = agnc_session_sqlite_meta_get(db, key, value_out);
    sqlite3_close(db);
    return status;
}

agnc_status_t agnc_session_meta_set(const char *path, const char *key, const char *value)
{
    sqlite3 *db = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || key == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_meta_set(db, key, value);
    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_exec_simple(db, "COMMIT");
    } else {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
    }

    sqlite3_close(db);
    return status;
}

agnc_status_t agnc_session_meta_delete(const char *path, const char *key)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    agnc_status_t status;
    int rc;

    if (path == NULL || key == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK || db == NULL) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_session_sqlite_exec_simple(db, AGNC_SESSION_SCHEMA);
    if (status != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return status;
    }

    if (agnc_session_sqlite_exec_simple(db, "BEGIN IMMEDIATE") != AGNC_STATUS_OK) {
        sqlite3_close(db);
        return AGNC_STATUS_IO_ERROR;
    }

    rc = sqlite3_prepare_v2(db, "DELETE FROM meta WHERE key = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        status = AGNC_STATUS_IO_ERROR;
    } else if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        status = AGNC_STATUS_IO_ERROR;
    } else {
        rc = sqlite3_step(stmt);
        status = rc == SQLITE_DONE ? AGNC_STATUS_OK : AGNC_STATUS_IO_ERROR;
    }

    sqlite3_finalize(stmt);

    if (status == AGNC_STATUS_OK) {
        status = agnc_session_sqlite_exec_simple(db, "COMMIT");
    } else {
        (void)agnc_session_sqlite_exec_simple(db, "ROLLBACK");
    }

    sqlite3_close(db);
    return status;
}

static double agnc_session_parse_meta_double(const char *text, double default_value)
{
    char *end = NULL;
    double value;

    if (text == NULL || text[0] == '\0') {
        return default_value;
    }

    value = strtod(text, &end);
    if (end == text) {
        return default_value;
    }

    return value;
}

agnc_status_t agnc_session_cost_load(const char *path, double *total_usd_out)
{
    char *text = NULL;

    if (total_usd_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *total_usd_out = 0.0;
    if (agnc_session_meta_get(path, "usage_cost_total_usd", &text) == AGNC_STATUS_OK && text != NULL) {
        *total_usd_out = agnc_session_parse_meta_double(text, 0.0);
    }
    free(text);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_cost_accumulate(const char *path, double delta_usd)
{
    double total = 0.0;
    char buffer[64];

    if (path == NULL || delta_usd <= 0.0) {
        return AGNC_STATUS_OK;
    }

    (void)agnc_session_cost_load(path, &total);
    total += delta_usd;
    snprintf(buffer, sizeof(buffer), "%.8f", total);
    return agnc_session_meta_set(path, "usage_cost_total_usd", buffer);
}

agnc_status_t agnc_session_cost_reset(const char *path)
{
    return agnc_session_meta_set(path, "usage_cost_total_usd", "0");
}
