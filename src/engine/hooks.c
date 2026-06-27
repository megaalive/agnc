/*
 * hooks.c
 *
 * Eksekusi hook shell per event; payload JSON via file sementara.
 */

#include "agnc/hooks.h"

#include "agnc/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define AGNC_HOOKS_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define AGNC_HOOKS_MKDIR(path) mkdir(path, 0755)
#endif

#define AGNC_HOOKS_MAX_PAYLOAD_BYTES (32 * 1024)
#define AGNC_HOOKS_MAX_COMMANDS_PER_EVENT 8

static const char *g_hook_event_names[AGNC_HOOK_EVENT_COUNT] = {
    "session_start",
    "pre_turn",
    "post_turn",
    "pre_tool",
    "post_tool",
};

const char *agnc_hooks_event_name(agnc_hook_event_id_t event_id)
{
    if (event_id < 0 || event_id >= AGNC_HOOK_EVENT_COUNT) {
        return "unknown";
    }

    return g_hook_event_names[event_id];
}

void agnc_hooks_free_payload(char *payload_json)
{
    free(payload_json);
}

char *agnc_hooks_build_payload_json(agnc_hook_event_id_t event_id, const agnc_hook_payload_input_t *input)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    char *result;

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "event", agnc_hooks_event_name(event_id));

    if (input != NULL) {
        if (input->session_name != NULL) {
            yyjson_mut_obj_add_str(doc, root, "session", input->session_name);
        }
        if (input->user_prompt != NULL) {
            yyjson_mut_obj_add_str(doc, root, "user_prompt", input->user_prompt);
        }
        if (input->tool_name != NULL) {
            yyjson_mut_obj_add_str(doc, root, "tool", input->tool_name);
        }
        if (input->tool_arguments != NULL) {
            yyjson_mut_obj_add_str(doc, root, "tool_arguments", input->tool_arguments);
        }
        if (input->tool_status != NULL) {
            yyjson_mut_obj_add_str(doc, root, "tool_status", input->tool_status);
        }
        if (input->provider_id != NULL) {
            yyjson_mut_obj_add_str(doc, root, "provider", input->provider_id);
        }
        if (input->model != NULL) {
            yyjson_mut_obj_add_str(doc, root, "model", input->model);
        }
        if (input->usage_prompt >= 0) {
            yyjson_mut_obj_add_sint(doc, root, "usage_prompt", input->usage_prompt);
        }
        if (input->usage_completion >= 0) {
            yyjson_mut_obj_add_sint(doc, root, "usage_completion", input->usage_completion);
        }
        if (input->usage_total >= 0) {
            yyjson_mut_obj_add_sint(doc, root, "usage_total", input->usage_total);
        }
    }

    result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static const char *const *agnc_hooks_commands_for_event(
    const agnc_config_t *config,
    agnc_hook_event_id_t event_id,
    size_t *count_out)
{
    if (count_out == NULL) {
        return NULL;
    }

    *count_out = 0;
    if (config == NULL || !config->hooks_enabled) {
        return NULL;
    }

    switch (event_id) {
    case AGNC_HOOK_EVENT_SESSION_START:
        *count_out = config->hooks_session_start_count;
        return (const char *const *)config->hooks_session_start;
    case AGNC_HOOK_EVENT_PRE_TURN:
        *count_out = config->hooks_pre_turn_count;
        return (const char *const *)config->hooks_pre_turn;
    case AGNC_HOOK_EVENT_POST_TURN:
        *count_out = config->hooks_post_turn_count;
        return (const char *const *)config->hooks_post_turn;
    case AGNC_HOOK_EVENT_PRE_TOOL:
        *count_out = config->hooks_pre_tool_count;
        return (const char *const *)config->hooks_pre_tool;
    case AGNC_HOOK_EVENT_POST_TOOL:
        *count_out = config->hooks_post_tool_count;
        return (const char *const *)config->hooks_post_tool;
    default:
        return NULL;
    }
}

size_t agnc_hooks_count_for_event(const agnc_config_t *config, agnc_hook_event_id_t event_id)
{
    size_t count = 0;
    (void)agnc_hooks_commands_for_event(config, event_id, &count);
    return count;
}

static char *agnc_hooks_strdup(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_hooks_write_payload_file(const char *payload_json, char **path_out)
{
    char *cache_dir = NULL;
    char path[1024];
    FILE *file;
    size_t length;
    agnc_status_t status;

    if (path_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *path_out = NULL;
    length = payload_json != NULL ? strlen(payload_json) : 0;
    if (length > AGNC_HOOKS_MAX_PAYLOAD_BYTES) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    status = agnc_path_expand_user("~/.agnc/cache", &cache_dir);
    if (status != AGNC_STATUS_OK || cache_dir == NULL) {
        free(cache_dir);
        return status != AGNC_STATUS_OK ? status : AGNC_STATUS_OUT_OF_MEMORY;
    }

    AGNC_HOOKS_MKDIR(cache_dir);
    snprintf(path, sizeof(path), "%s/hook_payload.json", cache_dir);
    free(cache_dir);

    file = fopen(path, "wb");
    if (file == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (length > 0 && fwrite(payload_json, 1, length, file) != length) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    fclose(file);
    *path_out = agnc_hooks_strdup(path);
    return *path_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
}

#ifdef _WIN32
static int agnc_hooks_run_command(const char *command, const char *event_name, const char *payload_path)
{
    char cmd_line[4096];
    int exit_code;

    if (command == NULL || command[0] == '\0') {
        return 0;
    }

    _putenv_s("AGNC_HOOK_EVENT", event_name != NULL ? event_name : "");
    _putenv_s("AGNC_HOOK_PAYLOAD_FILE", payload_path != NULL ? payload_path : "");

    snprintf(cmd_line, sizeof(cmd_line), "cmd /c %s", command);
    exit_code = system(cmd_line);
    if (exit_code == -1) {
        return -1;
    }

    return exit_code;
}
#else
static int agnc_hooks_run_command(const char *command, const char *event_name, const char *payload_path)
{
    char cmd_line[4096];

    if (command == NULL || command[0] == '\0') {
        return 0;
    }

    setenv("AGNC_HOOK_EVENT", event_name != NULL ? event_name : "", 1);
    setenv("AGNC_HOOK_PAYLOAD_FILE", payload_path != NULL ? payload_path : "", 1);

    snprintf(cmd_line, sizeof(cmd_line), "sh -c \"%s\"", command);
    return system(cmd_line);
}
#endif

agnc_status_t agnc_hooks_run(
    const agnc_config_t *config,
    agnc_hook_event_id_t event_id,
    const char *payload_json,
    int *blocked_out)
{
    const char *const *commands = NULL;
    size_t command_count = 0;
    size_t index;
    char *payload_path = NULL;
    const char *event_name;
    agnc_status_t status;
    int blocking_event = event_id == AGNC_HOOK_EVENT_PRE_TOOL;

    if (blocked_out != NULL) {
        *blocked_out = 0;
    }

    if (config == NULL || !config->hooks_enabled) {
        return AGNC_STATUS_OK;
    }

    commands = agnc_hooks_commands_for_event(config, event_id, &command_count);
    if (commands == NULL || command_count == 0) {
        return AGNC_STATUS_OK;
    }

    if (command_count > AGNC_HOOKS_MAX_COMMANDS_PER_EVENT) {
        command_count = AGNC_HOOKS_MAX_COMMANDS_PER_EVENT;
    }

    status = agnc_hooks_write_payload_file(payload_json != NULL ? payload_json : "{}", &payload_path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    event_name = agnc_hooks_event_name(event_id);

    for (index = 0; index < command_count; index++) {
        int exit_code = agnc_hooks_run_command(commands[index], event_name, payload_path);

        if (exit_code != 0) {
            if (config->verbose) {
                fprintf(
                    stderr,
                    "agnc: hook %s[%zu] exit %d: %s\n",
                    event_name,
                    index,
                    exit_code,
                    commands[index]);
            }

            if (blocking_event) {
                if (blocked_out != NULL) {
                    *blocked_out = 1;
                }
                free(payload_path);
                return AGNC_STATUS_TOOL_DENIED;
            }
        }
    }

    free(payload_path);
    return AGNC_STATUS_OK;
}
