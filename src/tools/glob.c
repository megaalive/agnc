/*
 * glob.c
 *
 * Tool glob: cari file yang cocok pola sederhana (* dan ?) di bawah direktori.
 * Rekursif dengan batas kedalaman agar tidak scan seluruh disk.
 */

#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#ifdef _WIN32
#include <windows.h>
#define AGNC_PATH_SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#define AGNC_PATH_SEP '/'
#endif

#define AGNC_GLOB_MAX_RESULTS 200
#define AGNC_GLOB_MAX_DEPTH 12
#define AGNC_GLOB_LINE_MAX 4096

typedef struct {
    char lines[AGNC_GLOB_MAX_RESULTS][AGNC_GLOB_LINE_MAX];
    size_t count;
    int truncated;
} agnc_glob_results_t;

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static int agnc_glob_match_simple(const char *pattern, const char *name)
{
    if (*pattern == '\0' && *name == '\0') {
        return 1;
    }

    if (*pattern == '*') {
        if (agnc_glob_match_simple(pattern + 1, name)) {
            return 1;
        }
        if (*name != '\0' && agnc_glob_match_simple(pattern, name + 1)) {
            return 1;
        }
        return 0;
    }

    if (*pattern == '\0' || *name == '\0') {
        return 0;
    }

    if (*pattern == '?' || *pattern == *name) {
        return agnc_glob_match_simple(pattern + 1, name + 1);
    }

    return 0;
}

static int agnc_glob_should_skip_dir(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
           strcmp(name, ".git") == 0 || strcmp(name, "node_modules") == 0 ||
           strcmp(name, "vcpkg_installed") == 0 || strcmp(name, "out") == 0 ||
           strcmp(name, ".vs") == 0;
}

static void agnc_glob_add_result(agnc_glob_results_t *results, const char *path)
{
    size_t len;

    if (results->count >= AGNC_GLOB_MAX_RESULTS) {
        results->truncated = 1;
        return;
    }

    len = strlen(path);
    if (len >= AGNC_GLOB_LINE_MAX) {
        len = AGNC_GLOB_LINE_MAX - 4;
        memcpy(results->lines[results->count], path, len);
        results->lines[results->count][len++] = '.';
        results->lines[results->count][len++] = '.';
        results->lines[results->count][len++] = '.';
        results->lines[results->count][len] = '\0';
    } else {
        snprintf(results->lines[results->count], AGNC_GLOB_LINE_MAX, "%s", path);
    }

    results->count++;
}

#ifdef _WIN32
static void agnc_glob_walk(
    const char *dir,
    const char *pattern,
    int depth,
    agnc_glob_results_t *results)
{
    char search[AGNC_GLOB_LINE_MAX];
    WIN32_FIND_DATAA entry;
    HANDLE handle;

    if (depth > AGNC_GLOB_MAX_DEPTH || results->truncated) {
        return;
    }

    snprintf(search, sizeof(search), "%s\\*", dir);
    handle = FindFirstFileA(search, &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        char child[AGNC_GLOB_LINE_MAX];
        int is_dir;

        if (agnc_glob_should_skip_dir(entry.cFileName)) {
            continue;
        }

        snprintf(child, sizeof(child), "%s\\%s", dir, entry.cFileName);
        is_dir = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (is_dir) {
            agnc_glob_walk(child, pattern, depth + 1, results);
        } else if (agnc_glob_match_simple(pattern, entry.cFileName)) {
            agnc_glob_add_result(results, child);
        }
    } while (FindNextFileA(handle, &entry) != 0);

    FindClose(handle);
}
#else
static void agnc_glob_walk(
    const char *dir,
    const char *pattern,
    int depth,
    agnc_glob_results_t *results)
{
    DIR *directory;
    struct dirent *entry;

    if (depth > AGNC_GLOB_MAX_DEPTH || results->truncated) {
        return;
    }

    directory = opendir(dir);
    if (directory == NULL) {
        return;
    }

    while ((entry = readdir(directory)) != NULL) {
        char child[AGNC_GLOB_LINE_MAX];
        struct stat stat_buf;
        int is_dir;

        if (agnc_glob_should_skip_dir(entry->d_name)) {
            continue;
        }

        snprintf(child, sizeof(child), "%s/%s", dir, entry->d_name);
        if (stat(child, &stat_buf) != 0) {
            continue;
        }

        is_dir = S_ISDIR(stat_buf.st_mode);
        if (is_dir) {
            agnc_glob_walk(child, pattern, depth + 1, results);
        } else if (agnc_glob_match_simple(pattern, entry->d_name)) {
            agnc_glob_add_result(results, child);
        }
    }

    closedir(directory);
}
#endif

static char *agnc_glob_format_results(const agnc_glob_results_t *results)
{
    size_t total = 0;
    size_t index;
    char *output;
    char *cursor;

    if (results->count == 0) {
        return agnc_strdup_local("(no matches)");
    }

    for (index = 0; index < results->count; index++) {
        total += strlen(results->lines[index]) + 1;
    }
    if (results->truncated) {
        total += 32;
    }

    output = (char *)malloc(total + 1);
    if (output == NULL) {
        return NULL;
    }

    cursor = output;
    for (index = 0; index < results->count; index++) {
        size_t line_len = strlen(results->lines[index]);
        memcpy(cursor, results->lines[index], line_len);
        cursor += line_len;
        *cursor++ = '\n';
    }

    if (results->truncated) {
        const char *suffix = "...(results truncated)\n";
        size_t suffix_len = strlen(suffix);
        memcpy(cursor, suffix, suffix_len);
        cursor += suffix_len;
    }

    *cursor = '\0';
    return output;
}

static agnc_status_t agnc_glob_parse(const char *arguments_json, char **pattern_out, char **dir_out)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *pattern_value;
    yyjson_val *dir_value;

    if (arguments_json == NULL || pattern_out == NULL || dir_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *pattern_out = NULL;
    *dir_out = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    pattern_value = yyjson_obj_get(root, "pattern");
    dir_value = yyjson_obj_get(root, "path");

    if (pattern_value == NULL || !yyjson_is_str(pattern_value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_TOOL_FAILED;
    }

    *pattern_out = agnc_strdup_local(yyjson_get_str(pattern_value));
    if (dir_value != NULL && yyjson_is_str(dir_value)) {
        *dir_out = agnc_strdup_local(yyjson_get_str(dir_value));
    } else {
        *dir_out = agnc_strdup_local(".");
    }

    yyjson_doc_free(doc);

    if (*pattern_out == NULL || *dir_out == NULL) {
        free(*pattern_out);
        free(*dir_out);
        *pattern_out = NULL;
        *dir_out = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_glob_execute(const char *arguments_json, char **result_text)
{
    char *pattern = NULL;
    char *dir = NULL;
    char *resolved = NULL;
    agnc_glob_results_t results;
    char *output;
    agnc_status_t status;
    const char *file_pattern;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;
    memset(&results, 0, sizeof(results));

    status = agnc_glob_parse(arguments_json, &pattern, &dir);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing pattern argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_resolve(dir, &resolved);
    if (status != AGNC_STATUS_OK) {
        free(pattern);
        free(dir);
        *result_text = agnc_strdup_local("error: cannot resolve directory");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_validate_workspace(resolved);
    if (status != AGNC_STATUS_OK) {
        free(pattern);
        free(dir);
        free(resolved);
        *result_text = agnc_strdup_local("error: directory outside workspace");
        return AGNC_STATUS_TOOL_DENIED;
    }

    /* Pola rekursif (mis. *.c): ambil bagian nama file setelah slash terakhir. */
    file_pattern = strrchr(pattern, '/');
    if (file_pattern != NULL) {
        file_pattern++;
    } else {
        file_pattern = pattern;
    }

    agnc_glob_walk(resolved, file_pattern, 0, &results);
    output = agnc_glob_format_results(&results);

    free(pattern);
    free(dir);
    free(resolved);

    if (output == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    *result_text = output;
    return AGNC_STATUS_OK;
}

const char *agnc_tool_glob_pattern_preview(const char *arguments_json)
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
    if (pattern_value == NULL) {
        pattern_value = yyjson_obj_get(root, "glob_pattern");
    }
    if (pattern_value != NULL && yyjson_is_str(pattern_value)) {
        snprintf(preview, sizeof(preview), "%s", yyjson_get_str(pattern_value));
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "(unknown)");
    return preview;
}
