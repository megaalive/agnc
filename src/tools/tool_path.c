/*
 * tool_path.c
 *
 * Resolusi path tool dengan fallback dari subfolder build,
 * plus validasi agar operasi file tidak keluar workspace.
 */

#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#else
#include <limits.h>
#include <unistd.h>
#endif

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_tool_path_get_workspace(void)
{
    const char *env_workspace;
    char buffer[PATH_MAX];

    env_workspace = getenv("AGNC_WORKSPACE");
    if (env_workspace != NULL && env_workspace[0] != '\0') {
        return agnc_strdup_local(env_workspace);
    }

    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        return NULL;
    }

    return agnc_strdup_local(buffer);
}

#ifdef _WIN32
static void agnc_tool_path_normalize_slashes(char *path)
{
    char *cursor;

    if (path == NULL) {
        return;
    }

    for (cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }
}

static int agnc_tool_path_prefix_match(const char *prefix, const char *path)
{
    size_t prefix_len;

    if (prefix == NULL || path == NULL) {
        return 0;
    }

    prefix_len = strlen(prefix);
    if (_strnicmp(prefix, path, prefix_len) != 0) {
        return 0;
    }

    if (path[prefix_len] == '\0' || path[prefix_len] == '/') {
        return 1;
    }

    return 0;
}

static agnc_status_t agnc_tool_path_to_absolute(const char *path, char **absolute)
{
    char full[PATH_MAX];
    char *result;

    if (path == NULL || absolute == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (_fullpath(full, path, PATH_MAX) == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    agnc_tool_path_normalize_slashes(full);
    result = agnc_strdup_local(full);
    if (result == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    *absolute = result;
    return AGNC_STATUS_OK;
}
#else
static int agnc_tool_path_prefix_match(const char *prefix, const char *path)
{
    size_t prefix_len;

    if (prefix == NULL || path == NULL) {
        return 0;
    }

    prefix_len = strlen(prefix);
    if (strncmp(prefix, path, prefix_len) != 0) {
        return 0;
    }

    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static agnc_status_t agnc_tool_path_to_absolute(const char *path, char **absolute)
{
    char *resolved;

    if (path == NULL || absolute == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        /* File belum ada (mis. write_file); bangun path absolut dari cwd. */
        char cwd[PATH_MAX];
        char combined[PATH_MAX * 2];

        if (path[0] == '/') {
            *absolute = agnc_strdup_local(path);
            return *absolute != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
        }

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return AGNC_STATUS_IO_ERROR;
        }

        snprintf(combined, sizeof(combined), "%s/%s", cwd, path);
        *absolute = agnc_strdup_local(combined);
        return *absolute != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    *absolute = resolved;
    return AGNC_STATUS_OK;
}
#endif

agnc_status_t agnc_tool_path_resolve(const char *path, char **resolved)
{
    static const char *prefixes[] = {
        "",
        "../../",
        "../../../",
        "../../../../",
        "../../../../../",
        "../../../../../../",
    };
    char candidate[PATH_MAX * 2];
    agnc_status_t status;
    size_t index;

    if (path == NULL || resolved == NULL || path[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    /* Tolak path dengan komponen .. eksplisit sebelum resolve. */
    if (strstr(path, "..") != NULL) {
        return AGNC_STATUS_TOOL_DENIED;
    }

    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        snprintf(candidate, sizeof(candidate), "%s%s", prefixes[index], path);
        status = agnc_tool_path_to_absolute(candidate, resolved);
        if (status == AGNC_STATUS_OK) {
            return status;
        }
    }

    return AGNC_STATUS_IO_ERROR;
}

agnc_status_t agnc_tool_path_validate_workspace(const char *absolute_path)
{
    char *workspace;
    char *workspace_abs;
    agnc_status_t status;
    int allowed;

    if (absolute_path == NULL || absolute_path[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    workspace = agnc_tool_path_get_workspace();
    if (workspace == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    status = agnc_tool_path_to_absolute(workspace, &workspace_abs);
    free(workspace);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    allowed = agnc_tool_path_prefix_match(workspace_abs, absolute_path);
    free(workspace_abs);

    return allowed ? AGNC_STATUS_OK : AGNC_STATUS_TOOL_DENIED;
}
