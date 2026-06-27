/*
 * config.h
 *
 * Struktur dan loader untuk file config global ~/.agnc.json.
 * Fase 1 hanya memuat field minimum yang dibutuhkan spike OpenRouter.
 */

#ifndef AGNC_CONFIG_H
#define AGNC_CONFIG_H

#include "agnc/status.h"

typedef struct {
    char *base_url;
    char *model;
    char *api_key;
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
