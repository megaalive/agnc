/*
 * config.c
 *
 * Loader config JSON: runtime, tools, permissions, dan resolusi provider Fase 3.
 */

#include "agnc/config.h"
#include "agnc/atomic_write.h"
#include "agnc/path.h"
#include "agnc/provider.h"

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

    if (size >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB &&
        (unsigned char)buffer[2] == 0xBF) {
        memmove(buffer, buffer + 3, (size_t)size - 3);
        buffer[size - 3] = '\0';
    }

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

/* Forward decl: dipakai agnc_config_find_dev_api_key_for_gateway sebelum definisi. */
static char *agnc_config_find_dev_api_key(void);

static char *agnc_config_get_env_value(const char *env_name)
{
    return agnc_config_get_env_strict(env_name);
}

static char *agnc_config_pick_env_chain(const char *const *names, size_t count)
{
    size_t index;
    char *value;

    for (index = 0; index < count; index++) {
        if (names[index] == NULL) {
            continue;
        }
        value = agnc_config_get_env_strict(names[index]);
        if (value != NULL) {
            return value;
        }
    }

    return NULL;
}

static char *agnc_config_find_dev_api_key_for_gateway(const char *gateway_id)
{
    char relative[128];
    static const char *prefixes[] = {
        "",
        "../",
        "../../",
        "../../../",
        "../../../../",
        "../../../../../",
    };
    size_t index;

    if (gateway_id == NULL || gateway_id[0] == '\0') {
        return NULL;
    }

    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        char *key;

        snprintf(relative, sizeof(relative), "%s.keys/%s.txt", prefixes[index], gateway_id);
        if (!agnc_path_exists(relative)) {
            continue;
        }
        key = agnc_config_read_api_key_from_file(relative);
        if (key != NULL) {
            return key;
        }
    }

    if (strcmp(gateway_id, "openrouter") == 0) {
        return agnc_config_find_dev_api_key();
    }

    return NULL;
}

static char *agnc_config_strdup_json_str(yyjson_val *value)
{
    if (value != NULL && yyjson_is_str(value)) {
        return agnc_strdup_local(yyjson_get_str(value));
    }
    return NULL;
}

static char *agnc_config_pick_string(
    const char *env_name,
    const char *primary,
    const char *secondary,
    const char *fallback)
{
    char *value;

    value = agnc_config_get_env_value(env_name);
    if (value != NULL) {
        return value;
    }
    if (primary != NULL && primary[0] != '\0') {
        return agnc_strdup_local(primary);
    }
    if (secondary != NULL && secondary[0] != '\0') {
        return agnc_strdup_local(secondary);
    }
    if (fallback != NULL && fallback[0] != '\0') {
        return agnc_strdup_local(fallback);
    }
    return NULL;
}

/*
 * Resolusi provider: provider.active + providers{} + descriptor gateway + env.
 * Mengisi provider_id, gateway_id, base_url, model, api_key di config.
 */
static agnc_status_t agnc_config_resolve_provider(yyjson_val *root, agnc_config_t *config)
{
    yyjson_val *provider_obj;
    yyjson_val *providers_obj;
    yyjson_val *entry;
    yyjson_val *value;
    const char *active;
    const char *gateway_id;
    const agnc_gateway_descriptor_t *gateway;
    char *active_owned = NULL;

    provider_obj = yyjson_obj_get(root, "provider");
    providers_obj = yyjson_obj_get(root, "providers");
    if (provider_obj == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    active = getenv("AGNC_PROVIDER");
    if (active == NULL || active[0] == '\0') {
        value = yyjson_obj_get(provider_obj, "active");
        if (value != NULL && yyjson_is_str(value)) {
            active = yyjson_get_str(value);
        }
    }
    if (active == NULL || active[0] == '\0') {
        active = "openrouter";
    }

    active_owned = agnc_strdup_local(active);
    if (active_owned == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    config->provider_id = active_owned;

    entry = NULL;
    if (providers_obj != NULL && yyjson_is_obj(providers_obj)) {
        entry = yyjson_obj_get(providers_obj, active);
    }

    value = entry != NULL ? yyjson_obj_get(entry, "gateway") : NULL;
    if (value != NULL && yyjson_is_str(value)) {
        gateway_id = yyjson_get_str(value);
    } else {
        gateway_id = active;
    }

    config->gateway_id = agnc_strdup_local(gateway_id);
    if (config->gateway_id == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    {
        char *provider_base = agnc_config_strdup_json_str(yyjson_obj_get(provider_obj, "base_url"));
        char *entry_base = agnc_config_strdup_json_str(yyjson_obj_get(entry, "base_url"));

        config->base_url = agnc_config_pick_string(
            "AGNC_BASE_URL", provider_base, entry_base, gateway->default_base_url);
        free(provider_base);
        free(entry_base);
    }

    {
        char *provider_model = agnc_config_strdup_json_str(yyjson_obj_get(provider_obj, "model"));
        char *entry_model = agnc_config_strdup_json_str(yyjson_obj_get(entry, "default_model"));

        config->model = agnc_config_pick_string(
            "AGNC_MODEL", provider_model, entry_model, gateway->default_model);
        free(provider_model);
        free(entry_model);
    }

    value = yyjson_obj_get(provider_obj, "api_key_file");
    if (value != NULL && yyjson_is_str(value)) {
        config->api_key = agnc_config_read_api_key_from_file(yyjson_get_str(value));
    }

    if (config->api_key == NULL && entry != NULL) {
        value = yyjson_obj_get(entry, "api_key_env");
        if (value != NULL && yyjson_is_str(value)) {
            config->api_key = agnc_config_get_env_strict(yyjson_get_str(value));
        }
    }

    if (config->api_key == NULL) {
        value = yyjson_obj_get(provider_obj, "api_key_env");
        if (value != NULL && yyjson_is_str(value)) {
            config->api_key = agnc_config_get_env_strict(yyjson_get_str(value));
        }
    }

    if (config->api_key == NULL && gateway->credential_env_vars != NULL) {
        config->api_key = agnc_config_pick_env_chain(
            gateway->credential_env_vars,
            gateway->credential_env_count);
    }

    if (config->api_key == NULL) {
        config->api_key = agnc_config_find_dev_api_key_for_gateway(config->gateway_id);
    }

    if (config->api_key == NULL) {
        config->api_key = agnc_config_get_env_fallbacks();
    }

    if (!gateway->requires_auth && config->api_key == NULL) {
        config->api_key = agnc_strdup_local("");
        if (config->api_key == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    if (config->model != NULL) {
        const char *resolved = agnc_provider_resolve_api_model(gateway, config->model);
        if (resolved != config->model) {
            char *copy = agnc_strdup_local(resolved);
            if (copy == NULL) {
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
            free(config->model);
            config->model = copy;
        }
    }

    return AGNC_STATUS_OK;
}

/* Cari kunci dev lokal OpenRouter (legacy path .keys/openrouter.txt). */
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
    yyjson_val *always_allow;

    tools = yyjson_obj_get(root, "tools");
    if (tools != NULL) {
        enabled = yyjson_obj_get(tools, "enabled");
        if (enabled != NULL && yyjson_is_arr(enabled)) {
            config->tool_read_file = agnc_config_json_array_contains(enabled, "read_file");
            config->tool_shell = agnc_config_json_array_contains(enabled, "shell");
            config->tool_write_file = agnc_config_json_array_contains(enabled, "write_file");
            config->tool_edit_file = agnc_config_json_array_contains(enabled, "edit_file");
            config->tool_grep = agnc_config_json_array_contains(enabled, "grep");
            config->tool_glob = agnc_config_json_array_contains(enabled, "glob");
            config->tool_web_fetch = agnc_config_json_array_contains(enabled, "web_fetch");
            config->tool_todo_write = agnc_config_json_array_contains(enabled, "todo_write");
            config->enable_tools = config->tool_read_file || config->tool_shell ||
                                   config->tool_write_file || config->tool_edit_file ||
                                   config->tool_grep || config->tool_glob ||
                                   config->tool_web_fetch || config->tool_todo_write;
        }
    }

    permissions = yyjson_obj_get(root, "permissions");
    if (permissions != NULL) {
        always_allow = yyjson_obj_get(permissions, "always_allow");
        if (always_allow != NULL && yyjson_is_arr(always_allow)) {
            if (agnc_config_json_array_contains(always_allow, "shell")) {
                config->ask_shell_permission = 0;
            }
            if (agnc_config_json_array_contains(always_allow, "write_file") ||
                agnc_config_json_array_contains(always_allow, "edit_file")) {
                config->ask_write_permission = 0;
            }
            if (agnc_config_json_array_contains(always_allow, "mcp")) {
                config->ask_mcp_permission = 0;
            }
            if (agnc_config_json_array_contains(always_allow, "web_fetch")) {
                config->ask_web_fetch_permission = 0;
            }
        }

        always_ask = yyjson_obj_get(permissions, "always_ask");
        if (always_ask != NULL && yyjson_is_arr(always_ask)) {
            int allow_shell =
                always_allow != NULL && yyjson_is_arr(always_allow) &&
                agnc_config_json_array_contains(always_allow, "shell");
            int allow_write =
                always_allow != NULL && yyjson_is_arr(always_allow) &&
                (agnc_config_json_array_contains(always_allow, "write_file") ||
                 agnc_config_json_array_contains(always_allow, "edit_file"));
            int allow_mcp =
                always_allow != NULL && yyjson_is_arr(always_allow) &&
                agnc_config_json_array_contains(always_allow, "mcp");
            int allow_web_fetch =
                always_allow != NULL && yyjson_is_arr(always_allow) &&
                agnc_config_json_array_contains(always_allow, "web_fetch");

            config->ask_shell_permission =
                agnc_config_json_array_contains(always_ask, "shell") && !allow_shell;
            config->ask_write_permission =
                (agnc_config_json_array_contains(always_ask, "write_file") ||
                 agnc_config_json_array_contains(always_ask, "edit_file")) &&
                !allow_write;
            config->ask_mcp_permission =
                agnc_config_json_array_contains(always_ask, "mcp") && !allow_mcp;
            config->ask_web_fetch_permission =
                agnc_config_json_array_contains(always_ask, "web_fetch") && !allow_web_fetch;
        }
    }
}

static void agnc_config_free_mcp_servers(agnc_config_t *config)
{
    size_t server_index;
    size_t arg_index;

    if (config == NULL || config->mcp_servers == NULL) {
        return;
    }

    for (server_index = 0; server_index < config->mcp_server_count; server_index++) {
        agnc_mcp_server_config_t *server = &config->mcp_servers[server_index];

        free(server->id);
        free(server->command);
        free(server->cwd);

        if (server->args != NULL) {
            for (arg_index = 0; arg_index < server->arg_count; arg_index++) {
                free(server->args[arg_index]);
            }
            free(server->args);
        }
    }

    free(config->mcp_servers);
    config->mcp_servers = NULL;
    config->mcp_server_count = 0;
}

static agnc_status_t agnc_config_parse_mcp_servers(yyjson_val *root, agnc_config_t *config)
{
    yyjson_val *mcp;
    yyjson_val *servers;
    size_t count;
    size_t index;

    mcp = yyjson_obj_get(root, "mcp");
    if (mcp == NULL) {
        return AGNC_STATUS_OK;
    }

    servers = yyjson_obj_get(mcp, "servers");
    if (servers == NULL) {
        return AGNC_STATUS_OK;
    }

    if (!yyjson_is_arr(servers)) {
        return AGNC_STATUS_JSON_ERROR;
    }

    count = yyjson_arr_size(servers);
    if (count == 0) {
        return AGNC_STATUS_OK;
    }

    config->mcp_servers = (agnc_mcp_server_config_t *)calloc(count, sizeof(*config->mcp_servers));
    if (config->mcp_servers == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    config->mcp_server_count = count;

    for (index = 0; index < count; index++) {
        yyjson_val *entry = yyjson_arr_get(servers, index);
        yyjson_val *value;
        yyjson_val *args;
        size_t arg_count;
        size_t arg_index;
        agnc_mcp_server_config_t *server = &config->mcp_servers[index];

        if (entry == NULL || !yyjson_is_obj(entry)) {
            agnc_config_free_mcp_servers(config);
            return AGNC_STATUS_JSON_ERROR;
        }

        server->enabled = 1;

        value = yyjson_obj_get(entry, "id");
        if (value != NULL && yyjson_is_str(value)) {
            server->id = agnc_strdup_local(yyjson_get_str(value));
            if (server->id == NULL) {
                agnc_config_free_mcp_servers(config);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
        }

        value = yyjson_obj_get(entry, "enabled");
        if (value != NULL && yyjson_is_bool(value)) {
            server->enabled = yyjson_get_bool(value) ? 1 : 0;
        }

        value = yyjson_obj_get(entry, "command");
        if (value != NULL && yyjson_is_str(value)) {
            server->command = agnc_strdup_local(yyjson_get_str(value));
            if (server->command == NULL) {
                agnc_config_free_mcp_servers(config);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
        }

        value = yyjson_obj_get(entry, "cwd");
        if (value != NULL && yyjson_is_str(value)) {
            server->cwd = agnc_strdup_local(yyjson_get_str(value));
            if (server->cwd == NULL) {
                agnc_config_free_mcp_servers(config);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
        }

        args = yyjson_obj_get(entry, "args");
        if (args != NULL) {
            if (!yyjson_is_arr(args)) {
                agnc_config_free_mcp_servers(config);
                return AGNC_STATUS_JSON_ERROR;
            }

            arg_count = yyjson_arr_size(args);
            if (arg_count > 0) {
                server->args = (char **)calloc(arg_count, sizeof(*server->args));
                if (server->args == NULL) {
                    agnc_config_free_mcp_servers(config);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }

                server->arg_count = arg_count;
                for (arg_index = 0; arg_index < arg_count; arg_index++) {
                    yyjson_val *arg_item = yyjson_arr_get(args, arg_index);

                    if (arg_item == NULL || !yyjson_is_str(arg_item)) {
                        agnc_config_free_mcp_servers(config);
                        return AGNC_STATUS_JSON_ERROR;
                    }

                    server->args[arg_index] = agnc_strdup_local(yyjson_get_str(arg_item));
                    if (server->args[arg_index] == NULL) {
                        agnc_config_free_mcp_servers(config);
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }
                }
            }
        }
    }

    return AGNC_STATUS_OK;
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
    config->tool_write_file = 1;
    config->tool_edit_file = 1;
    config->tool_grep = 1;
    config->tool_glob = 1;
    config->tool_web_fetch = 0;
    config->tool_todo_write = 0;
    config->ask_shell_permission = 1;
    config->ask_write_permission = 1;
    config->ask_mcp_permission = 1;
    config->ask_web_fetch_permission = 1;
}

void agnc_config_free(agnc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    agnc_config_free_mcp_servers(config);
    free(config->base_url);
    free(config->model);
    free(config->api_key);
    free(config->provider_id);
    free(config->gateway_id);
    config->base_url = NULL;
    config->model = NULL;
    config->api_key = NULL;
    config->provider_id = NULL;
    config->gateway_id = NULL;
}

agnc_status_t agnc_config_load(const char *path, agnc_config_t *config)
{
    char *default_path = NULL;
    const char *load_path = path;
    char *json_text = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
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
    runtime = yyjson_obj_get(root, "runtime");

    status = agnc_config_resolve_provider(root, config);
    if (status != AGNC_STATUS_OK) {
        yyjson_doc_free(doc);
        agnc_config_free(config);
        return status;
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

    status = agnc_config_parse_mcp_servers(root, config);
    if (status != AGNC_STATUS_OK) {
        yyjson_doc_free(doc);
        agnc_config_free(config);
        return status;
    }

    yyjson_doc_free(doc);

    {
        const agnc_gateway_descriptor_t *gateway = agnc_registry_find_gateway(config->gateway_id);

        if (gateway == NULL || config->base_url == NULL || config->model == NULL) {
            agnc_config_free(config);
            return AGNC_STATUS_INVALID_ARGUMENT;
        }

        if (gateway->requires_auth && (config->api_key == NULL || config->api_key[0] == '\0')) {
            agnc_config_free(config);
            return AGNC_STATUS_INVALID_ARGUMENT;
        }
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_config_save_json(const char *path, const char *json_text)
{
    char *default_path = NULL;
    const char *save_path = path;
    yyjson_doc *doc;
    size_t length;
    agnc_status_t status;

    if (json_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    doc = yyjson_read(json_text, strlen(json_text), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }
    yyjson_doc_free(doc);

    if (save_path == NULL) {
        status = agnc_path_default_config(&default_path);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
        save_path = default_path;
    }

    length = strlen(json_text);
    status = agnc_atomic_write_file(save_path, json_text, length);
    free(default_path);
    return status;
}
