/*
 * config.c
 *
 * Loader config JSON minimal untuk Fase 1 spike OpenRouter.
 */

#include "agnc/config.h"
#include "agnc/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_config_read_file(const char *path)
{
    FILE *file;
    long size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static char *agnc_config_extract_key_line(const char *text)
{
    const char *cursor = text;
    const char *start;
    const char *end;
    size_t length;
    char *key;

    while (*cursor != '\0') {
        if (strncmp(cursor, "sk-", 3) == 0) {
            start = cursor;
            end = cursor;
            while (*end != '\0' && *end != '\r' && *end != '\n' && *end != ' ') {
                end++;
            }
            length = (size_t)(end - start);
            key = (char *)malloc(length + 1);
            if (key == NULL) {
                return NULL;
            }
            memcpy(key, start, length);
            key[length] = '\0';
            return key;
        }
        cursor++;
    }

    return NULL;
}

static char *agnc_config_read_api_key_from_file(const char *path)
{
    char *text;
    char *key;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    text = agnc_config_read_file(path);
    if (text == NULL) {
        return NULL;
    }

    key = agnc_config_extract_key_line(text);
    free(text);
    return key;
}

static char *agnc_config_get_env_strict(const char *env_name)
{
    const char *value;

    if (env_name == NULL || env_name[0] == '\0') {
        return NULL;
    }

    value = getenv(env_name);
    if (value != NULL && value[0] != '\0') {
        return agnc_strdup_local(value);
    }

    return NULL;
}

static char *agnc_config_get_env_fallbacks(void)
{
    const char *value;

    value = getenv("AGNC_API_KEY");
    if (value != NULL && value[0] != '\0') {
        return agnc_strdup_local(value);
    }

    value = getenv("OPENROUTER_API_KEY");
    if (value != NULL && value[0] != '\0') {
        return agnc_strdup_local(value);
    }

    return NULL;
}

/* Cari kunci dev lokal saat dijalankan dari subfolder build (mis. out/build/x64-Debug). */
static char *agnc_config_find_dev_api_key(void)
{
    static const char *candidates[] = {
        ".keys/openrouter.txt",
        "../.keys/openrouter.txt",
        "../../.keys/openrouter.txt",
        "../../../.keys/openrouter.txt",
        "../../../../.keys/openrouter.txt",
        "../../../../../.keys/openrouter.txt",
    };
    size_t index;
    char *key;

    for (index = 0; index < sizeof(candidates) / sizeof(candidates[0]); index++) {
        if (!agnc_path_exists(candidates[index])) {
            continue;
        }

        key = agnc_config_read_api_key_from_file(candidates[index]);
        if (key != NULL) {
            return key;
        }
    }

    return NULL;
}

static int agnc_config_json_array_contains(yyjson_val *array, const char *name)
{
    size_t index;
    size_t count;
    yyjson_val *item;

    if (array == NULL || !yyjson_is_arr(array) || name == NULL) {
        return 0;
    }

    count = yyjson_arr_size(array);
    for (index = 0; index < count; index++) {
        item = yyjson_arr_get(array, index);
        if (item != NULL && yyjson_is_str(item) && strcmp(yyjson_get_str(item), name) == 0) {
            return 1;
        }
    }

    return 0;
}

static void agnc_config_apply_tools_permissions(yyjson_val *root, agnc_config_t *config)
{
    yyjson_val *tools;
    yyjson_val *permissions;
    yyjson_val *enabled;
    yyjson_val *always_ask;

    tools = yyjson_obj_get(root, "tools");
    if (tools != NULL) {
        enabled = yyjson_obj_get(tools, "enabled");
        if (enabled != NULL && yyjson_is_arr(enabled)) {
            config->tool_read_file = agnc_config_json_array_contains(enabled, "read_file");
            config->tool_shell = agnc_config_json_array_contains(enabled, "shell");
            config->enable_tools = config->tool_read_file || config->tool_shell;
        }
    }

    permissions = yyjson_obj_get(root, "permissions");
    if (permissions != NULL) {
        always_ask = yyjson_obj_get(permissions, "always_ask");
        if (always_ask != NULL && yyjson_is_arr(always_ask)) {
            config->ask_shell_permission = agnc_config_json_array_contains(always_ask, "shell");
        }
    }
}

void agnc_config_init(agnc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->max_tool_iterations = 25;
    config->stream = 1;
    config->enable_tools = 1;
    config->tool_read_file = 1;
    config->tool_shell = 1;
    config->ask_shell_permission = 1;
}

void agnc_config_free(agnc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    free(config->base_url);
    free(config->model);
    free(config->api_key);
    config->base_url = NULL;
    config->model = NULL;
    config->api_key = NULL;
}

agnc_status_t agnc_config_load(const char *path, agnc_config_t *config)
{
    char *default_path = NULL;
    const char *load_path = path;
    char *json_text = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *provider;
    yyjson_val *runtime;
    yyjson_val *value;
    agnc_status_t status = AGNC_STATUS_OK;

    if (config == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_config_free(config);
    agnc_config_init(config);

    if (load_path == NULL) {
        status = agnc_path_default_config(&default_path);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
        load_path = default_path;
    }

    json_text = agnc_config_read_file(load_path);
    free(default_path);

    if (json_text == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    doc = yyjson_read(json_text, strlen(json_text), 0);
    free(json_text);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    provider = yyjson_obj_get(root, "provider");
    runtime = yyjson_obj_get(root, "runtime");

    if (provider == NULL) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    value = yyjson_obj_get(provider, "base_url");
    if (value != NULL && yyjson_is_str(value)) {
        config->base_url = agnc_strdup_local(yyjson_get_str(value));
    }

    value = yyjson_obj_get(provider, "model");
    if (value != NULL && yyjson_is_str(value)) {
        config->model = agnc_strdup_local(yyjson_get_str(value));
    }

    value = yyjson_obj_get(provider, "api_key_file");
    if (value != NULL && yyjson_is_str(value)) {
        config->api_key = agnc_config_read_api_key_from_file(yyjson_get_str(value));
    }

    /*
     * Urutan resolusi API key:
     * 1. api_key_file dari config
     * 2. api_key_env (strict, tanpa fallback prematur)
     * 3. .keys/openrouter.txt (cwd + parent, untuk dev dari out/build/...)
     * 4. AGNC_API_KEY, lalu OPENROUTER_API_KEY
     */
    if (config->api_key == NULL) {
        value = yyjson_obj_get(provider, "api_key_env");
        if (value != NULL && yyjson_is_str(value)) {
            config->api_key = agnc_config_get_env_strict(yyjson_get_str(value));
        }
    }

    if (config->api_key == NULL) {
        config->api_key = agnc_config_find_dev_api_key();
    }

    if (config->api_key == NULL) {
        config->api_key = agnc_config_get_env_fallbacks();
    }

    if (runtime != NULL) {
        value = yyjson_obj_get(runtime, "max_tool_iterations");
        if (value != NULL && yyjson_is_int(value)) {
            config->max_tool_iterations = (int)yyjson_get_int(value);
        }

        value = yyjson_obj_get(runtime, "stream");
        if (value != NULL && yyjson_is_bool(value)) {
            config->stream = yyjson_get_bool(value) ? 1 : 0;
        }

        value = yyjson_obj_get(runtime, "verbose");
        if (value != NULL && yyjson_is_bool(value)) {
            config->verbose = yyjson_get_bool(value) ? 1 : 0;
        }
    }

    agnc_config_apply_tools_permissions(root, config);

    yyjson_doc_free(doc);

    if (config->base_url == NULL || config->model == NULL || config->api_key == NULL) {
        agnc_config_free(config);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    return AGNC_STATUS_OK;
}
