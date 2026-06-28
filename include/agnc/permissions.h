/*
 * permissions.h
 *
 * Gate permission interaktif untuk tool berisiko (shell, tulis/edit file).
 */

#ifndef AGNC_PERMISSIONS_H
#define AGNC_PERMISSIONS_H

#include "agnc/status.h"

/* Reset grant sesi (panggil saat REPL baru dimulai). */
void agnc_permission_session_reset(void);

/* Tanya pengguna di stdin; *allowed = 1 jika diizinkan. interactive_repl = mode chat agnc. */
agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed, int interactive_repl);

/*
 * Delegate izin untuk job background / headless: dipanggil dari worker thread,
 * implementasi menunggu jawaban di thread REPL utama. Return 1 = izinkan, 0 = tolak.
 */
typedef int (*agnc_permission_background_ask_fn)(const char *kind, const char *detail, void *ctx);

void agnc_permission_set_background_ask(agnc_permission_background_ask_fn fn, void *ctx);
void agnc_permission_clear_background_ask(void);

int agnc_permission_session_has_shell(void);

/* Prompt tulis/edit file; operation = "write" atau "edit". */
agnc_status_t agnc_permission_ask_file_write(const char *path, const char *operation, int *allowed, int interactive_repl);

agnc_status_t agnc_permission_ask_mcp(const char *tool_name, int *allowed, int interactive_repl);

agnc_status_t agnc_permission_ask_web_fetch(const char *url, int *allowed, int interactive_repl);

#endif
