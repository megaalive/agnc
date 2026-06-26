/*
 * tool.h
 *
 * Deklarasi tool internal agnc untuk Fase 1.
 */

#ifndef AGNC_TOOL_H
#define AGNC_TOOL_H

#include "agnc/status.h"

agnc_status_t agnc_tool_read_file_execute(const char *arguments_json, char **result_text);
agnc_status_t agnc_tool_shell_execute(const char *arguments_json, char **result_text);
const char *agnc_tool_shell_command_preview(const char *arguments_json);

#endif
