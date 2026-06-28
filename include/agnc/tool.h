/*
 * tool.h
 *
 * Deklarasi tool internal agnc (Fase 1–2).
 */

#ifndef AGNC_TOOL_H
#define AGNC_TOOL_H

#include "agnc/config.h"
#include "agnc/query.h"
#include "agnc/status.h"

agnc_status_t agnc_tool_read_file_execute(const char *arguments_json, char **result_text);
agnc_status_t agnc_tool_shell_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_shell_command_preview(const char *arguments_json);
/* Return 1 jika perintah shell adalah pencarian teks/file (harus pakai tool grep/glob). */
int agnc_tool_shell_is_search_command(const char *command);

/* Return 1 jika perintah shell berisiko tinggi (destructive/system). */
int agnc_tool_shell_is_dangerous_command(const char *command);

/* Ekstrak field command dari JSON tool shell; pemanggil free(*command_out). */
agnc_status_t agnc_tool_shell_extract_command(const char *arguments_json, char **command_out);

agnc_status_t agnc_tool_write_file_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_write_file_path_preview(const char *arguments_json);

agnc_status_t agnc_tool_edit_file_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_edit_file_path_preview(const char *arguments_json);

agnc_status_t agnc_tool_grep_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_grep_pattern_preview(const char *arguments_json);

agnc_status_t agnc_tool_glob_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_glob_pattern_preview(const char *arguments_json);

agnc_status_t agnc_tool_web_fetch_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_web_fetch_url_preview(const char *arguments_json);

agnc_status_t agnc_tool_todo_write_execute(const char *arguments_json, char **result_text);

agnc_status_t agnc_tool_find_symbol_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_find_symbol_name_preview(const char *arguments_json);

agnc_status_t agnc_tool_sub_agent_execute(
    const agnc_config_t *parent_config,
    const agnc_query_options_t *parent_options,
    int agent_depth,
    const char *arguments_json,
    char **result_text);

/* Invalidasi indeks ctags in-memory (setelah write/edit atau reset sesi). */
void agnc_find_symbol_index_invalidate(void);

#endif
