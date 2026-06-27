/*
 * config.h
 *
 * Struktur dan loader untuk file config global ~/.agnc.json.
 * Fase 3: resolusi provider.active + providers{} + descriptor gateway.
 */

#ifndef AGNC_CONFIG_H
#define AGNC_CONFIG_H

#include "agnc/status.h"

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
    int ask_shell_permission;
    int ask_write_permission;
} agnc_config_t;

void agnc_config_init(agnc_config_t *config);
void agnc_config_free(agnc_config_t *config);

/* Muat config dari path; jika path NULL, gunakan ~/.agnc.json. */
agnc_status_t agnc_config_load(const char *path, agnc_config_t *config);

/* Tulis teks JSON ke path dengan rename atomik; validasi JSON dulu. */
agnc_status_t agnc_config_save_json(const char *path, const char *json_text);

#endif
