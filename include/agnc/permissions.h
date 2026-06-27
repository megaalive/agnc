/*
 * permissions.h
 *
 * Gate permission interaktif untuk tool berisiko (shell, tulis/edit file).
 */

#ifndef AGNC_PERMISSIONS_H
#define AGNC_PERMISSIONS_H

#include "agnc/status.h"

/* Tanya pengguna di stdin; *allowed = 1 jika diizinkan. interactive_repl = mode chat agnc. */
agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed, int interactive_repl);

/* Prompt tulis/edit file; operation = "write" atau "edit". */
agnc_status_t agnc_permission_ask_file_write(const char *path, const char *operation, int *allowed, int interactive_repl);

agnc_status_t agnc_permission_ask_mcp(const char *tool_name, int *allowed, int interactive_repl);

#endif
