/*
 * tool_path.c
 *
 * Resolusi path tool dengan fallback dari subfolder build,
 * deteksi repo root otomatis, plus validasi workspace.
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

static int agnc_tool_path_exists(const char *path)
{
    DWORD attributes;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
}
#else
static int agnc_tool_path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    return access(path, F_OK) == 0;
}
#endif

#ifdef _WIN32
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

/* Naik satu level direktori; return 0 jika sudah di root. */
static int agnc_tool_path_pop_parent(char *path)
{
    size_t length;
    char *separator;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    length = strlen(path);
    while (length > 0 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        path[length - 1] = '\0';
        length--;
    }

    separator = strrchr(path, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(path, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator)) {
            separator = backslash;
        }
    }
#endif

    if (separator == NULL) {
        return 0;
    }

    if (separator == path) {
        return 0;
    }

#ifdef _WIN32
    if (path[1] == ':' && separator <= path + 2) {
        return 0;
    }
#endif

    *separator = '\0';
    return 1;
}

/*
 * Cari root repo dengan marker .git atau pasangan CMakeLists.txt + src/.
 * Penting saat agnc dijalankan dari out/build/x64-Debug.
 */
static char *agnc_tool_path_find_repo_root(const char *start_dir)
{
    char current[PATH_MAX];
    char marker_git[PATH_MAX];
    char marker_cmake[PATH_MAX];
    char marker_src[PATH_MAX];
    size_t depth;

    if (start_dir == NULL || start_dir[0] == '\0') {
        return NULL;
    }

    snprintf(current, sizeof(current), "%s", start_dir);

    for (depth = 0; depth < 12; depth++) {
        snprintf(marker_git, sizeof(marker_git), "%s/.git", current);
        if (agnc_tool_path_exists(marker_git)) {
            return agnc_strdup_local(current);
        }

        snprintf(marker_cmake, sizeof(marker_cmake), "%s/CMakeLists.txt", current);
        snprintf(marker_src, sizeof(marker_src), "%s/src", current);
        if (agnc_tool_path_exists(marker_cmake) && agnc_tool_path_exists(marker_src)) {
            return agnc_strdup_local(current);
        }

        if (!agnc_tool_path_pop_parent(current)) {
            break;
        }
    }

    return NULL;
}

static char *agnc_tool_path_get_workspace(void)
{
    const char *env_workspace;
    char buffer[PATH_MAX];
    char *repo_root;

    env_workspace = getenv("AGNC_WORKSPACE");
    if (env_workspace != NULL && env_workspace[0] != '\0') {
        return agnc_strdup_local(env_workspace);
    }

    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        return NULL;
    }

    repo_root = agnc_tool_path_find_repo_root(buffer);
    if (repo_root != NULL) {
        return repo_root;
    }

    return agnc_strdup_local(buffer);
}

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
    char *fallback = NULL;
    agnc_status_t status;
    size_t index;

    if (path == NULL || resolved == NULL || path[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *resolved = NULL;

    /* Tolak path dengan komponen .. eksplisit sebelum resolve. */
    if (strstr(path, "..") != NULL) {
        return AGNC_STATUS_TOOL_DENIED;
    }

    /*
     * Coba beberapa prefix parent; pilih path pertama yang benar-benar ada
     * (_fullpath di Windows sukses meski file/folder belum ada).
     */
    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        char *absolute = NULL;

        snprintf(candidate, sizeof(candidate), "%s%s", prefixes[index], path);
        status = agnc_tool_path_to_absolute(candidate, &absolute);
        if (status != AGNC_STATUS_OK) {
            continue;
        }

        if (agnc_tool_path_exists(absolute)) {
            free(fallback);
            *resolved = absolute;
            return AGNC_STATUS_OK;
        }

        /* Simpan kandidat paling dangkal untuk file baru yang belum ada. */
        if (fallback == NULL) {
            fallback = absolute;
        } else {
            free(absolute);
        }
    }

    if (fallback != NULL) {
        *resolved = fallback;
        return AGNC_STATUS_OK;
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

agnc_status_t agnc_tool_path_workspace_root(char **root)
{
    char *workspace;

    if (root == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    workspace = agnc_tool_path_get_workspace();
    if (workspace == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    return agnc_tool_path_to_absolute(workspace, root);
}

agnc_status_t agnc_tool_path_resolve_search(const char *path, char **resolved)
{
    char *src_path = NULL;
    agnc_status_t status;

    if (resolved == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *resolved = NULL;

    if (path != NULL && path[0] != '\0' && strcmp(path, ".") != 0) {
        return agnc_tool_path_resolve(path, resolved);
    }

    /* Model sering lupa path; default ke src/ jika ada di repo. */
    status = agnc_tool_path_resolve("src", &src_path);
    if (status == AGNC_STATUS_OK && src_path != NULL && agnc_tool_path_exists(src_path)) {
        *resolved = src_path;
        return AGNC_STATUS_OK;
    }

    free(src_path);
    return agnc_tool_path_workspace_root(resolved);
}
