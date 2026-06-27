/*
 * find_symbol.c
 *
 * Tool find_symbol via Universal Ctags — indeks simbol per workspace, lookup by name.
 */

#include "agnc/ctags_locate.h"
#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <strings.h>
#endif

#include <yyjson.h>

#ifdef _MSC_VER
#include <process.h>
#endif

#define AGNC_FIND_SYMBOL_MAX_MATCHES 50
#define AGNC_FIND_SYMBOL_MAX_OUTPUT (32 * 1024)

typedef struct {
    char *name;
    char *file;
    int line;
    char *kind;
} agnc_symbol_entry_t;

static char *g_symbol_index_root = NULL;
static agnc_symbol_entry_t *g_symbol_entries = NULL;
static size_t g_symbol_count = 0;

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_find_symbol_index_invalidate(void)
{
    size_t index;

    /* Indeks ctags per workspace; kosongkan setelah write/edit atau reset sesi. */
    for (index = 0; index < g_symbol_count; index++) {
        free(g_symbol_entries[index].name);
        free(g_symbol_entries[index].file);
        free(g_symbol_entries[index].kind);
    }

    free(g_symbol_entries);
    free(g_symbol_index_root);

    g_symbol_entries = NULL;
    g_symbol_index_root = NULL;
    g_symbol_count = 0;
}

static int agnc_find_symbol_name_matches(const char *entry_name, const char *query_name)
{
    if (entry_name == NULL || query_name == NULL) {
        return 0;
    }

    if (strcmp(entry_name, query_name) == 0) {
        return 1;
    }

#ifdef _WIN32
    return _stricmp(entry_name, query_name) == 0;
#else
    return strcasecmp(entry_name, query_name) == 0;
#endif
}

static agnc_status_t agnc_find_symbol_parse_line(const char *line, agnc_symbol_entry_t *entry)
{
    char *tab1;
    char *tab2;
    char *tab3;
    char line_buf[4096];
    size_t name_len;
    size_t file_len;
    size_t kind_len;
    char *semi;
    long parsed_line;

    if (line == NULL || entry == NULL || line[0] == '\0') {
        return AGNC_STATUS_TOOL_FAILED;
    }

    snprintf(line_buf, sizeof(line_buf), "%s", line);
    tab1 = strchr(line_buf, '\t');
    if (tab1 == NULL) {
        return AGNC_STATUS_TOOL_FAILED;
    }

    *tab1 = '\0';
    tab2 = strchr(tab1 + 1, '\t');
    if (tab2 == NULL) {
        return AGNC_STATUS_TOOL_FAILED;
    }

    *tab2 = '\0';
    tab3 = strchr(tab2 + 1, '\t');
    if (tab3 != NULL) {
        *tab3 = '\0';
    }

    semi = strchr(tab2 + 1, ';');
    if (semi != NULL) {
        *semi = '\0';
    }

    parsed_line = strtol(tab2 + 1, NULL, 10);
    if (parsed_line <= 0) {
        return AGNC_STATUS_TOOL_FAILED;
    }

    name_len = strlen(line_buf);
    file_len = strlen(tab1 + 1);
    kind_len = tab3 != NULL ? strlen(tab3 + 1) : 0;

    entry->name = (char *)malloc(name_len + 1);
    entry->file = (char *)malloc(file_len + 1);
    entry->kind = (char *)malloc(kind_len + 1);
    if (entry->name == NULL || entry->file == NULL || entry->kind == NULL) {
        free(entry->name);
        free(entry->file);
        free(entry->kind);
        entry->name = NULL;
        entry->file = NULL;
        entry->kind = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    memcpy(entry->name, line_buf, name_len + 1);
    memcpy(entry->file, tab1 + 1, file_len + 1);
    if (kind_len > 0) {
        memcpy(entry->kind, tab3 + 1, kind_len + 1);
    } else {
        entry->kind[0] = '\0';
    }
    entry->line = (int)parsed_line;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_find_symbol_build_index(const char *workspace_root)
{
    const char *ctags_binary;
    char *command;
    size_t command_len;
    FILE *pipe;
    char line[4096];
    agnc_symbol_entry_t *entries = NULL;
    size_t count = 0;
    size_t capacity = 256;
    agnc_status_t status = AGNC_STATUS_OK;

    ctags_binary = agnc_ctags_locate_binary();
    if (ctags_binary == NULL) {
        return AGNC_STATUS_TOOL_FAILED;
    }

    command_len = strlen(ctags_binary) + strlen(workspace_root) + 256;
    command = (char *)malloc(command_len);
    if (command == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

#ifdef _WIN32
    snprintf(
        command,
        command_len,
        "cd /d \"%s\" && \"%s\" -f - -n --fields=K --languages=C,C++ --exclude=.git --exclude=out "
        "--exclude=node_modules --exclude=build -R . 2>&1",
        workspace_root,
        ctags_binary);
#else
    snprintf(
        command,
        command_len,
        "cd \"%s\" && \"%s\" -f - -n --fields=K --languages=C,C++ --exclude=.git --exclude=out "
        "--exclude=node_modules --exclude=build -R . 2>&1",
        workspace_root,
        ctags_binary);
#endif

#ifdef _WIN32
    pipe = _popen(command, "rt");
#else
    pipe = popen(command, "r");
#endif
    free(command);

    if (pipe == NULL) {
        return AGNC_STATUS_TOOL_FAILED;
    }

    entries = (agnc_symbol_entry_t *)calloc(capacity, sizeof(*entries));
    if (entries == NULL) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    while (fgets(line, sizeof(line), pipe) != NULL) {
        agnc_symbol_entry_t parsed;
        size_t length;

        length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }

        if (length == 0 || line[0] == '!') {
            continue;
        }

        memset(&parsed, 0, sizeof(parsed));
        if (agnc_find_symbol_parse_line(line, &parsed) != AGNC_STATUS_OK) {
            continue;
        }

        if (count >= capacity) {
            agnc_symbol_entry_t *resized;
            size_t new_capacity = capacity * 2;

            resized = (agnc_symbol_entry_t *)realloc(entries, new_capacity * sizeof(*entries));
            if (resized == NULL) {
                free(parsed.name);
                free(parsed.file);
                free(parsed.kind);
                status = AGNC_STATUS_OUT_OF_MEMORY;
                break;
            }
            memset(resized + capacity, 0, (new_capacity - capacity) * sizeof(*resized));
            entries = resized;
            capacity = new_capacity;
        }

        entries[count++] = parsed;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    if (status != AGNC_STATUS_OK) {
        size_t index;
        for (index = 0; index < count; index++) {
            free(entries[index].name);
            free(entries[index].file);
            free(entries[index].kind);
        }
        free(entries);
        return status;
    }

    agnc_find_symbol_index_invalidate();
    g_symbol_entries = entries;
    g_symbol_count = count;
    g_symbol_index_root = agnc_strdup_local(workspace_root);
    if (g_symbol_index_root == NULL) {
        agnc_find_symbol_index_invalidate();
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_find_symbol_ensure_index(const char *workspace_root)
{
    if (workspace_root == NULL || workspace_root[0] == '\0') {
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (g_symbol_index_root != NULL && strcmp(g_symbol_index_root, workspace_root) == 0 && g_symbol_count > 0) {
        return AGNC_STATUS_OK;
    }

    /* Bangun ulang indeks ctags -R dari workspace root (sekali per sesi/path). */
    return agnc_find_symbol_build_index(workspace_root);
}

static agnc_status_t agnc_find_symbol_parse(
    const char *arguments_json,
    char **name_out,
    char **path_out)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *name_value;
    yyjson_val *path_value;

    if (arguments_json == NULL || name_out == NULL || path_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *name_out = NULL;
    *path_out = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    name_value = yyjson_obj_get(root, "name");
    path_value = yyjson_obj_get(root, "path");

    if (name_value == NULL || !yyjson_is_str(name_value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_TOOL_FAILED;
    }

    *name_out = agnc_strdup_local(yyjson_get_str(name_value));
    if (path_value != NULL && yyjson_is_str(path_value)) {
        *path_out = agnc_strdup_local(yyjson_get_str(path_value));
    } else {
        *path_out = agnc_strdup_local(".");
    }

    yyjson_doc_free(doc);

    if (*name_out == NULL || *path_out == NULL) {
        free(*name_out);
        free(*path_out);
        *name_out = NULL;
        *path_out = NULL;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static int agnc_find_symbol_path_prefix_matches(const char *file_path, const char *prefix)
{
    size_t prefix_len;

    if (prefix == NULL || prefix[0] == '\0' || strcmp(prefix, ".") == 0) {
        return 1;
    }

    prefix_len = strlen(prefix);
    if (strncmp(file_path, prefix, prefix_len) != 0) {
        return 0;
    }

    return file_path[prefix_len] == '\0' || file_path[prefix_len] == '/' || file_path[prefix_len] == '\\';
}

static agnc_status_t agnc_find_symbol_format_results(
    const char *query_name,
    const char *path_prefix,
    char **result_text)
{
    char *output;
    size_t total = 0;
    size_t matches = 0;
    size_t index;
    int truncated = 0;
    static const char header_fmt[] = "find_symbol results for '%s':\n";

    output = (char *)malloc(AGNC_FIND_SYMBOL_MAX_OUTPUT + 1);
    if (output == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    total += (size_t)snprintf(output, AGNC_FIND_SYMBOL_MAX_OUTPUT + 1, header_fmt, query_name);

    for (index = 0; index < g_symbol_count && matches < AGNC_FIND_SYMBOL_MAX_MATCHES; index++) {
        const agnc_symbol_entry_t *entry = &g_symbol_entries[index];
        char line[1024];
        size_t line_len;

        if (!agnc_find_symbol_name_matches(entry->name, query_name)) {
            continue;
        }
        if (!agnc_find_symbol_path_prefix_matches(entry->file, path_prefix)) {
            continue;
        }

        line_len = (size_t)snprintf(
            line,
            sizeof(line),
            "%s:%d %s %s\n",
            entry->file,
            entry->line,
            entry->kind[0] != '\0' ? entry->kind : "symbol",
            entry->name);

        if (total + line_len >= AGNC_FIND_SYMBOL_MAX_OUTPUT) {
            truncated = 1;
            break;
        }

        memcpy(output + total, line, line_len);
        total += line_len;
        matches++;
    }

    output[total] = '\0';

    if (matches == 0) {
        free(output);
        *result_text = agnc_strdup_local("(no matches)");
        return AGNC_STATUS_OK;
    }

    if (truncated) {
        static const char suffix[] = "\n...(output truncated)";
        size_t suffix_len = sizeof(suffix) - 1;

        if (total + suffix_len <= AGNC_FIND_SYMBOL_MAX_OUTPUT) {
            memcpy(output + total, suffix, suffix_len + 1);
        }
    }

    *result_text = output;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_find_symbol_execute(const char *arguments_json, char **result_text)
{
    char *name = NULL;
    char *path = NULL;
    char *resolved = NULL;
    char *workspace_root = NULL;
    const char *path_prefix;
    agnc_status_t status;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;

    status = agnc_find_symbol_parse(arguments_json, &name, &path);
    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: missing name argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_resolve(path, &resolved);
    if (status != AGNC_STATUS_OK) {
        free(name);
        free(path);
        *result_text = agnc_strdup_local("error: cannot resolve search path");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_tool_path_validate_workspace(resolved);
    if (status != AGNC_STATUS_OK) {
        free(name);
        free(path);
        free(resolved);
        *result_text = agnc_strdup_local("error: search path outside workspace");
        return AGNC_STATUS_TOOL_DENIED;
    }

    if (agnc_tool_path_workspace_root(&workspace_root) != AGNC_STATUS_OK || workspace_root == NULL) {
        free(name);
        free(path);
        free(resolved);
        *result_text = agnc_strdup_local("error: cannot resolve workspace root");
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (agnc_ctags_locate_binary() == NULL) {
        free(name);
        free(path);
        free(resolved);
        free(workspace_root);
        *result_text = agnc_strdup_local(
            "error: ctags not found. Install Universal Ctags or set AGNC_CTAGS_PATH. Do not use shell.");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_find_symbol_ensure_index(workspace_root);
    if (status != AGNC_STATUS_OK) {
        free(name);
        free(path);
        free(resolved);
        free(workspace_root);
        *result_text = agnc_strdup_local("error: ctags index build failed");
        return AGNC_STATUS_TOOL_FAILED;
    }

    path_prefix = path;
    if (path_prefix == NULL || path_prefix[0] == '\0' || strcmp(path_prefix, ".") == 0) {
        path_prefix = NULL;
    }

    status = agnc_find_symbol_format_results(name, path_prefix, result_text);

    free(name);
    free(path);
    free(resolved);
    free(workspace_root);
    return status;
}

const char *agnc_tool_find_symbol_name_preview(const char *arguments_json)
{
    static char preview[256];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *name_value;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    name_value = yyjson_obj_get(root, "name");
    if (name_value != NULL && yyjson_is_str(name_value)) {
        snprintf(preview, sizeof(preview), "%s", yyjson_get_str(name_value));
        yyjson_doc_free(doc);
        return preview;
    }

    yyjson_doc_free(doc);
    snprintf(preview, sizeof(preview), "(unknown)");
    return preview;
}
