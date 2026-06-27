/*
 * ctags_locate.c
 *
 * Lokasi binary ctags untuk tool find_symbol.
 */

#include "agnc/ctags_locate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static char g_ctags_path[1024];
static int g_ctags_path_ready = 0;

#ifdef _WIN32
static int agnc_ctags_path_exists(const char *path)
{
    DWORD attributes;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void agnc_ctags_store_if_valid(const char *path)
{
    if (path == NULL || path[0] == '\0' || g_ctags_path[0] != '\0') {
        return;
    }

    if (agnc_ctags_path_exists(path)) {
        snprintf(g_ctags_path, sizeof(g_ctags_path), "%s", path);
    }
}

static void agnc_ctags_try_where(const char *command)
{
    FILE *pipe;
    char line[1024];
    char shell_cmd[128];

    snprintf(shell_cmd, sizeof(shell_cmd), "where %s 2>nul", command);
    pipe = _popen(shell_cmd, "rt");
    if (pipe == NULL) {
        return;
    }

    if (fgets(line, sizeof(line), pipe) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        agnc_ctags_store_if_valid(line);
    }

    _pclose(pipe);
}

static void agnc_ctags_try_env_and_common_paths(void)
{
    const char *env_ctags;
    const char *user_profile;
    char candidate[1024];

    env_ctags = getenv("AGNC_CTAGS_PATH");
    if (env_ctags != NULL && env_ctags[0] != '\0') {
        agnc_ctags_store_if_valid(env_ctags);
        if (g_ctags_path[0] != '\0') {
            return;
        }
    }

    user_profile = getenv("USERPROFILE");
    if (user_profile != NULL) {
        snprintf(candidate, sizeof(candidate), "%s\\scoop\\shims\\ctags.exe", user_profile);
        agnc_ctags_store_if_valid(candidate);
        if (g_ctags_path[0] != '\0') {
            return;
        }

        snprintf(candidate, sizeof(candidate), "%s\\scoop\\shims\\universal-ctags.exe", user_profile);
        agnc_ctags_store_if_valid(candidate);
    }
}

static void agnc_ctags_try_path_env(void)
{
    const char *path_env;
    char *path_copy;
    char *cursor;
    const char *names[] = {"ctags.exe", "universal-ctags.exe", "uctags.exe"};
    size_t name_index;

    path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        return;
    }

    path_copy = _strdup(path_env);
    if (path_copy == NULL) {
        return;
    }

    cursor = path_copy;
    while (cursor != NULL && *cursor != '\0' && g_ctags_path[0] == '\0') {
        char *semi = strchr(cursor, ';');
        size_t length;
        char token[1024];

        if (semi != NULL) {
            *semi = '\0';
        }

        length = strlen(cursor);
        if (length > 0) {
            for (name_index = 0; name_index < sizeof(names) / sizeof(names[0]); name_index++) {
                snprintf(token, sizeof(token), "%s\\%s", cursor, names[name_index]);
                agnc_ctags_store_if_valid(token);
                if (g_ctags_path[0] != '\0') {
                    break;
                }
            }
        }

        cursor = semi != NULL ? semi + 1 : NULL;
    }

    free(path_copy);
}
#else
static void agnc_ctags_try_where(const char *command)
{
    FILE *pipe;
    char line[1024];
    char shell_cmd[128];

    snprintf(shell_cmd, sizeof(shell_cmd), "command -v %s 2>/dev/null", command);
    pipe = popen(shell_cmd, "r");
    if (pipe == NULL) {
        return;
    }

    if (fgets(line, sizeof(line), pipe) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length > 0 && g_ctags_path[0] == '\0') {
            snprintf(g_ctags_path, sizeof(g_ctags_path), "%s", line);
        }
    }

    pclose(pipe);
}

static void agnc_ctags_try_env_and_common_paths(void)
{
    const char *env_ctags = getenv("AGNC_CTAGS_PATH");
    if (env_ctags != NULL && env_ctags[0] != '\0') {
        snprintf(g_ctags_path, sizeof(g_ctags_path), "%s", env_ctags);
    }
}

static void agnc_ctags_try_path_env(void)
{
    (void)0;
}
#endif

const char *agnc_ctags_locate_binary(void)
{
    static const char *where_names[] = {"ctags", "universal-ctags", "uctags"};
    size_t index;

    if (g_ctags_path_ready) {
        return g_ctags_path[0] != '\0' ? g_ctags_path : NULL;
    }

    g_ctags_path[0] = '\0';
    agnc_ctags_try_env_and_common_paths();
    for (index = 0; index < sizeof(where_names) / sizeof(where_names[0]); index++) {
        if (g_ctags_path[0] != '\0') {
            break;
        }
        agnc_ctags_try_where(where_names[index]);
    }
    if (g_ctags_path[0] == '\0') {
        agnc_ctags_try_path_env();
    }

    g_ctags_path_ready = 1;
    return g_ctags_path[0] != '\0' ? g_ctags_path : NULL;
}
