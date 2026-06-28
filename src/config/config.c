/*
 * config.c
 *
 * Loader config JSON: runtime, tools, permissions, dan resolusi provider Fase 3.
 */

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/atomic_write.h"
#include "agnc/oauth.h"
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

/* Baris pertama (trim whitespace) = API key; baris berikutnya diabaikan. */
static char *agnc_config_extract_key_line(const char *text)
{
    const char *line_start;
    const char *line_end;
    size_t length;
    char *key;

    if (text == NULL || text[0] == '\0') {
        return NULL;
    }

    line_start = text;
    while (*line_start == ' ' || *line_start == '\t') {
        line_start++;
    }

    if (*line_start == '\0' || *line_start == '\r' || *line_start == '\n') {
        return NULL;
    }

    line_end = line_start;
    while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
        line_end++;
    }

    while (line_end > line_start && (line_end[-1] == ' ' || line_end[-1] == '\t')) {
        line_end--;
    }

    length = (size_t)(line_end - line_start);
    if (length == 0) {
        return NULL;
    }

    key = (char *)malloc(length + 1);
    if (key == NULL) {
        return NULL;
    }
    memcpy(key, line_start, length);
    key[length] = '\0';
    return key;
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
typedef struct {
    const char *force_active; /* Non-NULL: pakai id ini, abaikan provider.active / AGNC_PROVIDER. */
    int ignore_base_url_env;  /* Abaikan AGNC_BASE_URL (tetap hormati providers.{id}.base_url). */
    int ignore_model_env;     /* Abaikan AGNC_MODEL (tetap hormati providers.{id}.default_model). */
} agnc_config_provider_opts_t;

static agnc_status_t agnc_config_resolve_provider(
    yyjson_val *root,
    agnc_config_t *config,
    const agnc_config_provider_opts_t *opts)
{
    yyjson_val *provider_obj;
    yyjson_val *providers_obj;
    yyjson_val *entry;
    yyjson_val *value;
    const char *active;
    const char *gateway_id;
    const agnc_gateway_descriptor_t *gateway;
    char *active_owned = NULL;
    int ignore_base_url_env = 0;
    int ignore_model_env = 0;

    if (opts != NULL) {
        ignore_base_url_env = opts->ignore_base_url_env;
        ignore_model_env = opts->ignore_model_env;
    }

    provider_obj = yyjson_obj_get(root, "provider");
    providers_obj = yyjson_obj_get(root, "providers");
    if (provider_obj == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    if (opts != NULL && opts->force_active != NULL && opts->force_active[0] != '\0') {
        active = opts->force_active;
    } else {
        active = getenv("AGNC_PROVIDER");
        if (active == NULL || active[0] == '\0') {
            value = yyjson_obj_get(provider_obj, "active");
            if (value != NULL && yyjson_is_str(value)) {
                active = yyjson_get_str(value);
            }
        }
    }
    if (active == NULL || active[0] == '\0') {
        active = "openrouter";
    }

    active_owned = agnc_strdup_local(active);
    if (active_owned == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    free(config->provider_id);
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

    free(config->gateway_id);
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

        free(config->base_url);
        if (entry_base != NULL && entry_base[0] != '\0') {
            config->base_url = entry_base;
            entry_base = NULL;
        } else if (!ignore_base_url_env) {
            config->base_url = agnc_config_pick_string(
                "AGNC_BASE_URL", provider_base, NULL, gateway->default_base_url);
        } else if (provider_base != NULL && provider_base[0] != '\0') {
            config->base_url = provider_base;
            provider_base = NULL;
        } else {
            config->base_url = agnc_strdup_local(gateway->default_base_url);
        }
        free(provider_base);
        free(entry_base);
        if (config->base_url == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    {
        char *provider_model = agnc_config_strdup_json_str(yyjson_obj_get(provider_obj, "model"));
        char *entry_model = agnc_config_strdup_json_str(yyjson_obj_get(entry, "default_model"));

        free(config->model);
        if (entry_model != NULL && entry_model[0] != '\0') {
            config->model = entry_model;
            entry_model = NULL;
        } else if (!ignore_model_env) {
            config->model = agnc_config_pick_string(
                "AGNC_MODEL", provider_model, NULL, gateway->default_model);
        } else if (provider_model != NULL && provider_model[0] != '\0') {
            config->model = provider_model;
            provider_model = NULL;
        } else {
            config->model = agnc_strdup_local(gateway->default_model);
        }
        free(provider_model);
        free(entry_model);
        if (config->model == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    free(config->api_key);
    config->api_key = NULL;

    if (entry != NULL) {
        value = yyjson_obj_get(entry, "api_key_file");
        if (value != NULL && yyjson_is_str(value)) {
            config->api_key = agnc_config_read_api_key_from_file(yyjson_get_str(value));
        }
    }

    if (config->api_key == NULL) {
        value = yyjson_obj_get(provider_obj, "api_key_file");
        if (value != NULL && yyjson_is_str(value)) {
            config->api_key = agnc_config_read_api_key_from_file(yyjson_get_str(value));
        }
    }

    if (config->api_key == NULL && entry != NULL) {
        value = yyjson_obj_get(entry, "oauth");
        if (value != NULL && yyjson_is_bool(value) && yyjson_get_bool(value) && config->provider_id != NULL) {
            if (agnc_oauth_load_fresh_access_token(config->provider_id, &config->api_key) != AGNC_STATUS_OK) {
                agnc_oauth_token_t oauth_token;

                agnc_oauth_token_init(&oauth_token);
                if (agnc_oauth_token_load(config->provider_id, &oauth_token) == AGNC_STATUS_OK &&
                    oauth_token.access_token != NULL) {
                    config->api_key = oauth_token.access_token;
                    oauth_token.access_token = NULL;
                }
                agnc_oauth_token_free(&oauth_token);
            }
        }
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

    if (config->api_key == NULL && gateway->requires_auth) {
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

static void agnc_config_free_skills_paths(agnc_config_t *config)
{
    size_t index;

    if (config == NULL) {
        return;
    }

    for (index = 0; index < config->skills_path_count; index++) {
        free(config->skills_paths[index]);
    }
    free(config->skills_paths);
    config->skills_paths = NULL;
    config->skills_path_count = 0;
}

static agnc_status_t agnc_config_parse_skills(yyjson_val *root, agnc_config_t *config)
{
    yyjson_val *skills;
    yyjson_val *enabled;
    yyjson_val *paths;
    size_t index;
    size_t count;

    skills = yyjson_obj_get(root, "skills");
    if (skills == NULL) {
        return AGNC_STATUS_OK;
    }

    enabled = yyjson_obj_get(skills, "enabled");
    if (enabled != NULL && yyjson_is_bool(enabled)) {
        config->skills_enabled = yyjson_get_bool(enabled) ? 1 : 0;
    }

    paths = yyjson_obj_get(skills, "paths");
    if (paths == NULL || !yyjson_is_arr(paths)) {
        return AGNC_STATUS_OK;
    }

    count = yyjson_arr_size(paths);
    if (count == 0) {
        return AGNC_STATUS_OK;
    }

    config->skills_paths = (char **)calloc(count, sizeof(*config->skills_paths));
    if (config->skills_paths == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < count; index++) {
        yyjson_val *item = yyjson_arr_get(paths, index);
        if (item == NULL || !yyjson_is_str(item)) {
            continue;
        }

        config->skills_paths[config->skills_path_count] = agnc_strdup_local(yyjson_get_str(item));
        if (config->skills_paths[config->skills_path_count] == NULL) {
            agnc_config_free_skills_paths(config);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        config->skills_path_count++;
    }

    return AGNC_STATUS_OK;
}

static void agnc_config_free_hook_commands(char ***commands, size_t *count)
{
    size_t index;

    if (commands == NULL || *commands == NULL) {
        if (count != NULL) {
            *count = 0;
        }
        return;
    }

    for (index = 0; index < *count; index++) {
        free((*commands)[index]);
    }
    free(*commands);
    *commands = NULL;
    if (count != NULL) {
        *count = 0;
    }
}

static void agnc_config_free_hooks(agnc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    agnc_config_free_hook_commands(&config->hooks_session_start, &config->hooks_session_start_count);
    agnc_config_free_hook_commands(&config->hooks_pre_turn, &config->hooks_pre_turn_count);
    agnc_config_free_hook_commands(&config->hooks_post_turn, &config->hooks_post_turn_count);
    agnc_config_free_hook_commands(&config->hooks_pre_tool, &config->hooks_pre_tool_count);
    agnc_config_free_hook_commands(&config->hooks_post_tool, &config->hooks_post_tool_count);
}

static agnc_status_t agnc_config_parse_hook_event_array(
    yyjson_val *hooks_root,
    const char *event_key,
    char ***commands_out,
    size_t *count_out)
{
    yyjson_val *array;
    size_t index;
    size_t count;

    if (commands_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *commands_out = NULL;
    *count_out = 0;

    if (hooks_root == NULL) {
        return AGNC_STATUS_OK;
    }

    array = yyjson_obj_get(hooks_root, event_key);
    if (array == NULL || !yyjson_is_arr(array)) {
        return AGNC_STATUS_OK;
    }

    count = yyjson_arr_size(array);
    if (count == 0) {
        return AGNC_STATUS_OK;
    }

    *commands_out = (char **)calloc(count, sizeof(**commands_out));
    if (*commands_out == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < count; index++) {
        yyjson_val *item = yyjson_arr_get(array, index);
        if (item == NULL || !yyjson_is_str(item)) {
            continue;
        }

        (*commands_out)[*count_out] = agnc_strdup_local(yyjson_get_str(item));
        if ((*commands_out)[*count_out] == NULL) {
            agnc_config_free_hook_commands(commands_out, count_out);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        (*count_out)++;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_config_parse_hooks(yyjson_val *root, agnc_config_t *config)
{
    yyjson_val *hooks;
    yyjson_val *enabled;
    agnc_status_t status;

    hooks = yyjson_obj_get(root, "hooks");
    if (hooks == NULL) {
        return AGNC_STATUS_OK;
    }

    enabled = yyjson_obj_get(hooks, "enabled");
    if (enabled != NULL && yyjson_is_bool(enabled)) {
        config->hooks_enabled = yyjson_get_bool(enabled) ? 1 : 0;
    }

    status = agnc_config_parse_hook_event_array(
        hooks, "session_start", &config->hooks_session_start, &config->hooks_session_start_count);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_config_parse_hook_event_array(hooks, "pre_turn", &config->hooks_pre_turn, &config->hooks_pre_turn_count);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_config_parse_hook_event_array(
        hooks, "post_turn", &config->hooks_post_turn, &config->hooks_post_turn_count);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_config_parse_hook_event_array(hooks, "pre_tool", &config->hooks_pre_tool, &config->hooks_pre_tool_count);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_config_parse_hook_event_array(
        hooks, "post_tool", &config->hooks_post_tool, &config->hooks_post_tool_count);
    return status;
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
            config->tool_find_symbol = agnc_config_json_array_contains(enabled, "find_symbol");
            config->tool_sub_agent = agnc_config_json_array_contains(enabled, "sub_agent");
            config->enable_tools = config->tool_read_file || config->tool_shell ||
                                   config->tool_write_file || config->tool_edit_file ||
                                   config->tool_grep || config->tool_glob ||
                                   config->tool_web_fetch || config->tool_todo_write ||
                                   config->tool_find_symbol || config->tool_sub_agent;
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

        {
            yyjson_val *always_deny = yyjson_obj_get(permissions, "always_deny");

            if (always_deny != NULL && yyjson_is_arr(always_deny)) {
                if (agnc_config_json_array_contains(always_deny, "shell")) {
                    config->deny_shell_permission = 1;
                    config->ask_shell_permission = 0;
                }
                if (agnc_config_json_array_contains(always_deny, "write_file") ||
                    agnc_config_json_array_contains(always_deny, "edit_file")) {
                    config->deny_write_permission = 1;
                    config->ask_write_permission = 0;
                }
                if (agnc_config_json_array_contains(always_deny, "mcp")) {
                    config->deny_mcp_permission = 1;
                    config->ask_mcp_permission = 0;
                }
                if (agnc_config_json_array_contains(always_deny, "web_fetch")) {
                    config->deny_web_fetch_permission = 1;
                    config->ask_web_fetch_permission = 0;
                }
            }
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

        if (server->env_keys != NULL) {
            for (arg_index = 0; arg_index < server->env_count; arg_index++) {
                free(server->env_keys[arg_index]);
                free(server->env_values[arg_index]);
            }
            free(server->env_keys);
            free(server->env_values);
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

        {
            yyjson_val *env_object = yyjson_obj_get(entry, "env");
            yyjson_obj_iter env_iter;

            if (env_object != NULL) {
                size_t env_index = 0;

                if (!yyjson_is_obj(env_object)) {
                    agnc_config_free_mcp_servers(config);
                    return AGNC_STATUS_JSON_ERROR;
                }

                server->env_count = yyjson_obj_size(env_object);
                if (server->env_count > 0) {
                    server->env_keys = (char **)calloc(server->env_count, sizeof(*server->env_keys));
                    server->env_values = (char **)calloc(server->env_count, sizeof(*server->env_values));
                    if (server->env_keys == NULL || server->env_values == NULL) {
                        free(server->env_keys);
                        free(server->env_values);
                        server->env_keys = NULL;
                        server->env_values = NULL;
                        agnc_config_free_mcp_servers(config);
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }

                    yyjson_obj_iter_init(env_object, &env_iter);
                    while ((value = yyjson_obj_iter_next(&env_iter)) != NULL) {
                        const char *env_key = yyjson_get_str(value);

                        value = yyjson_obj_iter_get_val(value);

                        if (env_key == NULL || env_key[0] == '\0' || value == NULL || !yyjson_is_str(value)) {
                            agnc_config_free_mcp_servers(config);
                            return AGNC_STATUS_JSON_ERROR;
                        }

                        server->env_keys[env_index] = agnc_strdup_local(env_key);
                        server->env_values[env_index] = agnc_strdup_local(yyjson_get_str(value));
                        if (server->env_keys[env_index] == NULL || server->env_values[env_index] == NULL) {
                            agnc_config_free_mcp_servers(config);
                            return AGNC_STATUS_OUT_OF_MEMORY;
                        }

                        env_index++;
                    }

                    server->env_count = env_index;
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
    config->tool_find_symbol = 1;
    config->tool_sub_agent = 1;
    config->skills_enabled = 1;
    config->hooks_enabled = 0;
    config->tui_enabled = 0;
    config->sessions_restore_routing = 0;
    config->sessions_auto_compact = 1;
    config->sessions_auto_compact_threshold = AGNC_SESSION_AUTO_COMPACT_THRESHOLD_MESSAGES;
    config->sessions_auto_compact_keep = AGNC_CONVERSATION_COMPACT_KEEP;
    config->sessions_auto_compact_threshold_tokens = AGNC_SESSION_AUTO_COMPACT_THRESHOLD_TOKENS;
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
    agnc_config_free_skills_paths(config);
    agnc_config_free_hooks(config);
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

static const char AGNC_CONFIG_BOOTSTRAP_JSON[] =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"provider\": {\n"
    "    \"active\": \"ollama\"\n"
    "  },\n"
    "  \"providers\": {\n"
    "    \"ollama\": {\n"
    "      \"gateway\": \"ollama\",\n"
    "      \"base_url\": \"http://127.0.0.1:11434/v1\",\n"
    "      \"default_model\": \"qwen2.5-coder:7b\"\n"
    "    },\n"
    "    \"openrouter\": {\n"
    "      \"gateway\": \"openrouter\",\n"
    "      \"base_url\": \"https://openrouter.ai/api/v1\",\n"
    "      \"api_key_env\": \"AGNC_API_KEY\",\n"
    "      \"default_model\": \"openrouter/owl-alpha\"\n"
    "    }\n"
    "  },\n"
    "  \"permissions\": {\n"
    "    \"mode\": \"default\",\n"
    "    \"always_allow\": [],\n"
    "    \"always_deny\": [],\n"
    "    \"always_ask\": [\"shell\", \"write_file\", \"edit_file\", \"mcp\", \"web_fetch\"]\n"
    "  },\n"
    "  \"mcp\": {\n"
    "    \"servers\": []\n"
    "  },\n"
    "  \"tools\": {\n"
    "    \"enabled\": [\n"
    "      \"shell\", \"read_file\", \"write_file\", \"edit_file\", \"grep\", \"glob\",\n"
    "      \"find_symbol\", \"web_fetch\", \"todo_write\", \"sub_agent\"\n"
    "    ]\n"
    "  },\n"
    "  \"skills\": {\n"
    "    \"enabled\": true,\n"
    "    \"paths\": [\"~/.agnc/skills\", \".agnc/skills\"]\n"
    "  },\n"
    "  \"hooks\": {\n"
    "    \"enabled\": false,\n"
    "    \"session_start\": [],\n"
    "    \"pre_turn\": [],\n"
    "    \"post_turn\": [],\n"
    "    \"pre_tool\": [],\n"
    "    \"post_tool\": []\n"
    "  },\n"
    "  \"runtime\": {\n"
    "    \"max_tool_iterations\": 25,\n"
    "    \"stream\": true,\n"
    "    \"verbose\": false,\n"
    "    \"tui\": false\n"
    "  },\n"
    "  \"sessions\": {\n"
    "    \"restore_routing\": false,\n"
    "    \"auto_compact\": true,\n"
    "    \"auto_compact_threshold\": 32,\n"
    "    \"auto_compact_keep\": 24,\n"
    "    \"auto_compact_threshold_tokens\": 100000\n"
    "  },\n"
    "  \"paths\": {\n"
    "    \"sessions_dir\": \"~/.agnc/sessions\",\n"
    "    \"cache_dir\": \"~/.agnc/cache\"\n"
    "  }\n"
    "}\n";

static agnc_status_t agnc_config_bootstrap_dirs(void)
{
    char *sessions = NULL;
    char *cache = NULL;
    char *skills = NULL;
    agnc_status_t status;

    status = agnc_path_expand_user("~/.agnc/sessions", &sessions);
    if (status != AGNC_STATUS_OK) {
        free(sessions);
        return status;
    }

    status = agnc_path_expand_user("~/.agnc/cache", &cache);
    if (status != AGNC_STATUS_OK) {
        free(sessions);
        free(cache);
        return status;
    }

    status = agnc_path_expand_user("~/.agnc/skills", &skills);
    if (status != AGNC_STATUS_OK) {
        free(sessions);
        free(cache);
        free(skills);
        return status;
    }

    if (agnc_path_ensure_dir(sessions) != AGNC_STATUS_OK ||
        agnc_path_ensure_dir(cache) != AGNC_STATUS_OK ||
        agnc_path_ensure_dir(skills) != AGNC_STATUS_OK) {
        free(sessions);
        free(cache);
        free(skills);
        return AGNC_STATUS_IO_ERROR;
    }

    free(sessions);
    free(cache);
    free(skills);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_config_bootstrap_if_missing(const char *path, int *created_out)
{
    char *default_path = NULL;
    const char *bootstrap_path = path;
    agnc_status_t status;

    if (created_out != NULL) {
        *created_out = 0;
    }

    if (bootstrap_path == NULL) {
        status = agnc_path_default_config(&default_path);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
        bootstrap_path = default_path;
    }

    if (agnc_path_exists(bootstrap_path)) {
        free(default_path);
        return AGNC_STATUS_OK;
    }

    status = agnc_config_bootstrap_dirs();
    if (status != AGNC_STATUS_OK) {
        free(default_path);
        return status;
    }

    status = agnc_config_save_json(bootstrap_path, AGNC_CONFIG_BOOTSTRAP_JSON);
    if (status == AGNC_STATUS_OK && created_out != NULL) {
        *created_out = 1;
    }

    free(default_path);
    return status;
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
    int bootstrapped = 0;

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

    status = agnc_config_bootstrap_if_missing(load_path, &bootstrapped);
    if (status != AGNC_STATUS_OK) {
        free(default_path);
        return status;
    }

    if (bootstrapped) {
        fprintf(
            stderr,
            "agnc: config baru dibuat di %s (provider default: ollama)\n"
            "agnc: edit API key/provider di file itu, lalu jalankan `agnc doctor`\n",
            load_path);
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
    {
        yyjson_val *sessions = yyjson_obj_get(root, "sessions");

        if (sessions != NULL) {
            value = yyjson_obj_get(sessions, "restore_routing");
            if (value != NULL && yyjson_is_bool(value)) {
                config->sessions_restore_routing = yyjson_get_bool(value) ? 1 : 0;
            }

            value = yyjson_obj_get(sessions, "auto_compact");
            if (value != NULL && yyjson_is_bool(value)) {
                config->sessions_auto_compact = yyjson_get_bool(value) ? 1 : 0;
            }

            value = yyjson_obj_get(sessions, "auto_compact_threshold");
            if (value != NULL && yyjson_is_int(value)) {
                int threshold = (int)yyjson_get_int(value);
                if (threshold > 0) {
                    config->sessions_auto_compact_threshold = threshold;
                }
            }

            value = yyjson_obj_get(sessions, "auto_compact_keep");
            if (value != NULL && yyjson_is_int(value)) {
                int keep = (int)yyjson_get_int(value);
                if (keep > 0) {
                    config->sessions_auto_compact_keep = keep;
                }
            }

            value = yyjson_obj_get(sessions, "auto_compact_threshold_tokens");
            if (value != NULL && yyjson_is_int(value)) {
                config->sessions_auto_compact_threshold_tokens = (long)yyjson_get_int(value);
            }
        }
    }

    status = agnc_config_resolve_provider(root, config, NULL);
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

        value = yyjson_obj_get(runtime, "tui");
        if (value != NULL && yyjson_is_bool(value)) {
            config->tui_enabled = yyjson_get_bool(value) ? 1 : 0;
        }
    }

    agnc_config_apply_tools_permissions(root, config);

    status = agnc_config_parse_skills(root, config);
    if (status != AGNC_STATUS_OK) {
        yyjson_doc_free(doc);
        agnc_config_free(config);
        return status;
    }

    status = agnc_config_parse_hooks(root, config);
    if (status != AGNC_STATUS_OK) {
        yyjson_doc_free(doc);
        agnc_config_free(config);
        return status;
    }

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

static agnc_status_t agnc_config_read_root(const char *path, yyjson_doc **doc_out, yyjson_val **root_out)
{
    char *default_path = NULL;
    const char *load_path = path;
    char *json_text = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;

    if (doc_out == NULL || root_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *doc_out = NULL;
    *root_out = NULL;

    if (load_path == NULL) {
        agnc_status_t status = agnc_path_default_config(&default_path);
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
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    *doc_out = doc;
    *root_out = root;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_config_list_provider_ids(const char *path, char ***ids_out, size_t *count_out)
{
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *providers_obj;
    yyjson_obj_iter iter;
    char **ids = NULL;
    size_t count = 0;
    agnc_status_t status;

    if (ids_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *ids_out = NULL;
    *count_out = 0;

    status = agnc_config_read_root(path, &doc, &root);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    providers_obj = yyjson_obj_get(root, "providers");
    if (providers_obj == NULL || !yyjson_is_obj(providers_obj)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    count = yyjson_obj_size(providers_obj);
    if (count == 0) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_OK;
    }

    ids = (char **)calloc(count, sizeof(char *));
    if (ids == NULL) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    yyjson_obj_iter_init(providers_obj, &iter);
    count = 0;
    while (yyjson_obj_iter_has_next(&iter)) {
        yyjson_val *key_val = yyjson_obj_iter_next(&iter);
        const char *key;

        if (key_val == NULL) {
            continue;
        }

        key = yyjson_get_str(key_val);
        if (key == NULL || key[0] == '\0') {
            continue;
        }

        ids[count] = agnc_strdup_local(key);
        if (ids[count] == NULL) {
            agnc_config_free_provider_id_list(ids, count);
            yyjson_doc_free(doc);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        count++;
    }

    yyjson_doc_free(doc);
    *ids_out = ids;
    *count_out = count;
    return AGNC_STATUS_OK;
}

void agnc_config_free_provider_id_list(char **ids, size_t count)
{
    size_t index;

    if (ids == NULL) {
        return;
    }

    for (index = 0; index < count; index++) {
        free(ids[index]);
    }
    free(ids);
}

agnc_status_t agnc_config_load_provider_entry(const char *path, const char *provider_id, agnc_config_t *config)
{
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    agnc_config_provider_opts_t opts;
    agnc_status_t status;
    const agnc_gateway_descriptor_t *gateway;

    if (provider_id == NULL || provider_id[0] == '\0' || config == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    status = agnc_config_read_root(path, &doc, &root);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    memset(&opts, 0, sizeof(opts));
    opts.force_active = provider_id;
    opts.ignore_base_url_env = 1;
    opts.ignore_model_env = 1;

    status = agnc_config_resolve_provider(root, config, &opts);
    yyjson_doc_free(doc);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL || config->base_url == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (gateway->requires_auth && (config->api_key == NULL || config->api_key[0] == '\0')) {
        return AGNC_STATUS_INVALID_ARGUMENT;
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

agnc_status_t agnc_config_set_runtime_verbose(const char *path, int enabled)
{
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_mut_doc *mut_doc = NULL;
    yyjson_mut_val *mut_root;
    yyjson_mut_val *runtime;
    char *json_text = NULL;
    agnc_status_t status;

    status = agnc_config_read_root(path, &doc, &root);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    mut_doc = yyjson_mut_doc_new(NULL);
    if (mut_doc == NULL) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    mut_root = yyjson_val_mut_copy(mut_doc, root);
    yyjson_doc_free(doc);
    if (mut_root == NULL) {
        yyjson_mut_doc_free(mut_doc);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }
    yyjson_mut_doc_set_root(mut_doc, mut_root);

    runtime = yyjson_mut_obj_get(mut_root, "runtime");
    if (runtime == NULL || !yyjson_mut_is_obj(runtime)) {
        runtime = yyjson_mut_obj(mut_doc);
        if (runtime == NULL) {
            yyjson_mut_doc_free(mut_doc);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        yyjson_mut_obj_add_val(mut_doc, mut_root, "runtime", runtime);
    }

    if (yyjson_mut_obj_get(runtime, "verbose") != NULL) {
        yyjson_mut_obj_remove_key(runtime, "verbose");
    }
    if (!yyjson_mut_obj_add_bool(mut_doc, runtime, "verbose", enabled ? true : false)) {
        yyjson_mut_doc_free(mut_doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    json_text = yyjson_mut_write(mut_doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(mut_doc);
    if (json_text == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_config_save_json(path, json_text);
    free(json_text);
    return status;
}
