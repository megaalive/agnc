/*
 * tool_cache.c
 *
 * Cache LRU sederhana untuk hasil tool read-only per sesi REPL.
 */

#include "agnc/tool_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AGNC_TOOL_CACHE_MAX 32

typedef struct {
    char *key;
    char *result;
} agnc_tool_cache_entry_t;

static agnc_tool_cache_entry_t g_entries[AGNC_TOOL_CACHE_MAX];
static size_t g_entry_count = 0;

static char *agnc_tool_cache_strdup(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static void agnc_tool_cache_entry_free(agnc_tool_cache_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }

    free(entry->key);
    free(entry->result);
    entry->key = NULL;
    entry->result = NULL;
}

static char *agnc_tool_cache_make_key(const char *tool_name, const char *arguments_json)
{
    size_t name_len;
    size_t args_len;
    char *key;

    if (tool_name == NULL) {
        tool_name = "";
    }
    if (arguments_json == NULL) {
        arguments_json = "{}";
    }

    name_len = strlen(tool_name);
    args_len = strlen(arguments_json);
    key = (char *)malloc(name_len + 1 + args_len + 1);
    if (key == NULL) {
        return NULL;
    }

    memcpy(key, tool_name, name_len);
    key[name_len] = '\n';
    memcpy(key + name_len + 1, arguments_json, args_len + 1);
    return key;
}

static void agnc_tool_cache_move_to_front(size_t index)
{
    agnc_tool_cache_entry_t moved;
    size_t slot;

    /* LRU: entri yang baru dipakai/ditulis pindah ke slot 0. */
    if (index == 0 || index >= g_entry_count) {
        return;
    }

    moved = g_entries[index];
    for (slot = index; slot > 0; slot--) {
        g_entries[slot] = g_entries[slot - 1];
    }
    g_entries[0] = moved;
}

void agnc_tool_cache_reset(void)
{
    size_t index;

    for (index = 0; index < g_entry_count; index++) {
        agnc_tool_cache_entry_free(&g_entries[index]);
    }
    g_entry_count = 0;
}

int agnc_tool_cache_is_eligible(const char *tool_name)
{
    if (tool_name == NULL) {
        return 0;
    }

    return strcmp(tool_name, "grep") == 0 || strcmp(tool_name, "glob") == 0 ||
           strcmp(tool_name, "read_file") == 0 || strcmp(tool_name, "find_symbol") == 0;
}

int agnc_tool_cache_get(const char *tool_name, const char *arguments_json, char **result_out)
{
    char *key;
    size_t index;

    if (result_out == NULL) {
        return 0;
    }

    *result_out = NULL;
    if (!agnc_tool_cache_is_eligible(tool_name)) {
        return 0;
    }

    key = agnc_tool_cache_make_key(tool_name, arguments_json);
    if (key == NULL) {
        return 0;
    }

    for (index = 0; index < g_entry_count; index++) {
        if (g_entries[index].key != NULL && strcmp(g_entries[index].key, key) == 0) {
            *result_out = agnc_tool_cache_strdup(g_entries[index].result);
            free(key);
            if (*result_out == NULL) {
                return 0;
            }
            agnc_tool_cache_move_to_front(index);
            return 1;
        }
    }

    free(key);
    return 0;
}

agnc_status_t agnc_tool_cache_put(const char *tool_name, const char *arguments_json, const char *result)
{
    char *key;
    char *copy;
    size_t index;

    if (!agnc_tool_cache_is_eligible(tool_name) || result == NULL) {
        return AGNC_STATUS_OK;
    }

    key = agnc_tool_cache_make_key(tool_name, arguments_json);
    if (key == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    copy = agnc_tool_cache_strdup(result);
    if (copy == NULL) {
        free(key);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < g_entry_count; index++) {
        if (g_entries[index].key != NULL && strcmp(g_entries[index].key, key) == 0) {
            free(g_entries[index].result);
            g_entries[index].result = copy;
            free(key);
            agnc_tool_cache_move_to_front(index);
            return AGNC_STATUS_OK;
        }
    }

    if (g_entry_count >= AGNC_TOOL_CACHE_MAX) {
        agnc_tool_cache_entry_free(&g_entries[g_entry_count - 1]);
        g_entry_count--;
    }

    for (index = g_entry_count; index > 0; index--) {
        g_entries[index] = g_entries[index - 1];
    }

    g_entries[0].key = key;
    g_entries[0].result = copy;
    g_entry_count++;
    return AGNC_STATUS_OK;
}
