/*
 * rg_locate.c
 *
 * Lokasi binary ripgrep untuk tool grep.
 * Mencoba AGNC_RG_PATH, where rg, PATH manual, dan lokasi Cursor/VS Code.
 */

#include "agnc/rg_locate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define AGNC_PATH_SEP '\\'
#else
#include <unistd.h>
#include <limits.h>
#define AGNC_PATH_SEP '/'
#endif

static char g_rg_path[1024];
static int g_rg_path_ready = 0;

#ifdef _WIN32
static int agnc_rg_path_exists(const char *path)
{
    DWORD attributes;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void agnc_rg_store_if_valid(const char *path)
{
    if (path == NULL || path[0] == '\0' || g_rg_path[0] != '\0') {
        return;
    }

    if (agnc_rg_path_exists(path)) {
        snprintf(g_rg_path, sizeof(g_rg_path), "%s", path);
    }
}

static void agnc_rg_try_where(void)
{
    FILE *pipe;
    char line[1024];

    pipe = _popen("where rg 2>nul", "rt");
    if (pipe == NULL) {
        return;
    }

    if (fgets(line, sizeof(line), pipe) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        agnc_rg_store_if_valid(line);
    }

    _pclose(pipe);
}

static void agnc_rg_try_env_and_common_paths(void)
{
    const char *env_rg;
    const char *local_app;
    const char *user_profile;
    char candidate[1024];

    env_rg = getenv("AGNC_RG_PATH");
    if (env_rg != NULL && env_rg[0] != '\0') {
        agnc_rg_store_if_valid(env_rg);
        if (g_rg_path[0] != '\0') {
            return;
        }
    }

    local_app = getenv("LOCALAPPDATA");
    if (local_app != NULL) {
        snprintf(
            candidate,
            sizeof(candidate),
            "%s\\Programs\\cursor\\resources\\app\\node_modules\\@vscode\\ripgrep\\bin\\rg.exe",
            local_app);
        agnc_rg_store_if_valid(candidate);
        if (g_rg_path[0] != '\0') {
            return;
        }

        snprintf(
            candidate,
            sizeof(candidate),
            "%s\\Programs\\Microsoft VS Code\\resources\\app\\node_modules\\@vscode\\ripgrep\\bin\\rg.exe",
            local_app);
        agnc_rg_store_if_valid(candidate);
        if (g_rg_path[0] != '\0') {
            return;
        }
    }

    user_profile = getenv("USERPROFILE");
    if (user_profile != NULL) {
        snprintf(candidate, sizeof(candidate), "%s\\scoop\\shims\\rg.exe", user_profile);
        agnc_rg_store_if_valid(candidate);
        if (g_rg_path[0] != '\0') {
            return;
        }
    }

    agnc_rg_store_if_valid("d:\\ai-ide\\cursor\\resources\\app\\node_modules\\@vscode\\ripgrep\\bin\\rg.exe");
}

static void agnc_rg_try_path_env(void)
{
    const char *path_env;
    char *path_copy;
    char *cursor;
    char token[1024];

    path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        return;
    }

    path_copy = _strdup(path_env);
    if (path_copy == NULL) {
        return;
    }

    cursor = path_copy;
    while (cursor != NULL && *cursor != '\0') {
        char *semi = strchr(cursor, ';');
        size_t length;

        if (semi != NULL) {
            *semi = '\0';
        }

        length = strlen(cursor);
        if (length > 0) {
            snprintf(token, sizeof(token), "%s\\rg.exe", cursor);
            agnc_rg_store_if_valid(token);
            if (g_rg_path[0] != '\0') {
                break;
            }
        }

        cursor = semi != NULL ? semi + 1 : NULL;
    }

    free(path_copy);
}
#else
static void agnc_rg_try_where(void)
{
    FILE *pipe;
    char line[1024];

    pipe = popen("command -v rg 2>/dev/null", "r");
    if (pipe == NULL) {
        return;
    }

    if (fgets(line, sizeof(line), pipe) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length > 0) {
            snprintf(g_rg_path, sizeof(g_rg_path), "%s", line);
        }
    }

    pclose(pipe);
}

static void agnc_rg_try_env_and_common_paths(void)
{
    const char *env_rg = getenv("AGNC_RG_PATH");
    if (env_rg != NULL && env_rg[0] != '\0') {
        snprintf(g_rg_path, sizeof(g_rg_path), "%s", env_rg);
    }
}

static void agnc_rg_try_path_env(void)
{
    (void)0;
}
#endif

const char *agnc_rg_locate_binary(void)
{
    if (g_rg_path_ready) {
        return g_rg_path[0] != '\0' ? g_rg_path : NULL;
    }

    g_rg_path[0] = '\0';
    agnc_rg_try_env_and_common_paths();
    if (g_rg_path[0] == '\0') {
        agnc_rg_try_where();
    }
    if (g_rg_path[0] == '\0') {
        agnc_rg_try_path_env();
    }

    g_rg_path_ready = 1;
    return g_rg_path[0] != '\0' ? g_rg_path : NULL;
}
