/*
 * config.h
 *
 * Struktur dan loader untuk file config global ~/.agnc.json.
 * Fase 3: resolusi provider.active + providers{} + descriptor gateway.
 */

#ifndef AGNC_CONFIG_H
#define AGNC_CONFIG_H

#include "agnc/status.h"

#include <stddef.h>

typedef struct {
    char *id;
    char *command;
    char **args;
    size_t arg_count;
    char *cwd;
    char **env_keys;
    char **env_values;
    size_t env_count;
    int enabled;
} agnc_mcp_server_config_t;

typedef struct {
    char *base_url;
    char *model;
    char *api_key;
    char *provider_id;  /* Kunci instance di providers{} config, mis. openrouter */
    char *gateway_id;   /* Id descriptor gateway di registry */
    int max_tool_iterations;
    int stream;
    int verbose;
    int enable_tools;
    int tool_read_file;
    int tool_shell;
    int tool_write_file;
    int tool_edit_file;
    int tool_grep;
    int tool_glob;
    int tool_web_fetch;
    int tool_todo_write;
    int tool_find_symbol;
    int ask_shell_permission;
    int ask_write_permission;
    int ask_mcp_permission;
    int ask_web_fetch_permission;
    int deny_shell_permission;
    int deny_write_permission;
    int deny_mcp_permission;
    int deny_web_fetch_permission;
    int skills_enabled;
    char **skills_paths;
    size_t skills_path_count;
    int hooks_enabled;
    char **hooks_session_start;
    size_t hooks_session_start_count;
    char **hooks_pre_turn;
    size_t hooks_pre_turn_count;
    char **hooks_post_turn;
    size_t hooks_post_turn_count;
    char **hooks_pre_tool;
    size_t hooks_pre_tool_count;
    char **hooks_post_tool;
    size_t hooks_post_tool_count;
    agnc_mcp_server_config_t *mcp_servers;
    size_t mcp_server_count;
} agnc_config_t;

void agnc_config_init(agnc_config_t *config);
void agnc_config_free(agnc_config_t *config);

/* Muat config dari path; jika path NULL, gunakan ~/.agnc.json. */
agnc_status_t agnc_config_load(const char *path, agnc_config_t *config);

/*
 * Daftar kunci providers{} dari config (mis. openrouter, ollama, opencode).
 * ids_out dan setiap string dialokasikan; pemanggil memanggil agnc_config_free_provider_id_list().
 */
agnc_status_t agnc_config_list_provider_ids(const char *path, char ***ids_out, size_t *count_out);
void agnc_config_free_provider_id_list(char **ids, size_t count);

/*
 * Resolusi base_url/model/api_key/gateway untuk satu entri providers{id}.
 * Mengabaikan AGNC_PROVIDER/AGNC_BASE_URL/AGNC_MODEL agar discovery per-provider konsisten.
 * Hanya field provider yang diisi ulang di config; field lain tidak disentuh.
 */
agnc_status_t agnc_config_load_provider_entry(const char *path, const char *provider_id, agnc_config_t *config);

/* Tulis teks JSON ke path dengan rename atomik; validasi JSON dulu. */
agnc_status_t agnc_config_save_json(const char *path, const char *json_text);

#endif
