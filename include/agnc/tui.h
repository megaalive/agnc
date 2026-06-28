/*
 * tui.h
 *
 * TUI REPL bertahap — ANSI VT murni (tanpa notcurses/curses).
 * Status bar, scroll region, panel tool/jobs opsional.
 */

#ifndef AGNC_TUI_H
#define AGNC_TUI_H

#include <stddef.h>

typedef enum {
    AGNC_TUI_VIEW_NORMAL = 0,
    AGNC_TUI_VIEW_TOOLS = 1,
    AGNC_TUI_VIEW_JOBS = 2
} agnc_tui_view_mode_t;

typedef struct {
    const char *model;
    const char *session;
    long last_turn_tokens;
    int queue_jobs;
    int running_jobs;
    const char *toast;
} agnc_tui_status_t;

void agnc_tui_init(int enabled);
void agnc_tui_shutdown(void);

int agnc_tui_is_active(void);

void agnc_tui_set_view(agnc_tui_view_mode_t mode);
agnc_tui_view_mode_t agnc_tui_get_view(void);

void agnc_tui_update_status(const agnc_tui_status_t *status);
void agnc_tui_set_toast(const char *message);

void agnc_tui_tool_log(const char *line);

/* Panggil sebelum printf(">\\n") — atur scroll region + status/panel bawah. */
void agnc_tui_before_prompt(void);

/* before_prompt + prompt `>` (ganti printf(">\\n") di REPL). */
void agnc_tui_show_prompt(void);

/* Pastikan output chat tetap di scroll region (no-op jika TUI off). */
void agnc_tui_begin_chat_output(void);

/* Blok output panjang (slash command): tahan repaint chrome sampai end. */
void agnc_tui_begin_scroll_output(void);
void agnc_tui_end_scroll_output(void);

/* Spinner di status bar (TUI) — di-drive dari agnc_console_spinner_*. */
void agnc_tui_set_spinner(int active);
void agnc_tui_spinner_tick(void);

/* Bersihkan layar + scrollback (startup / Ctrl+C). */
void agnc_tui_clear_screen(void);

/* Posisi prompt line-edit: row/col 1-based; 0 jika TUI off. */
void agnc_tui_get_prompt_pos(int *row, int *col);

/* Pulihkan status bar setelah line-edit menulis ke footer (Win32). */
void agnc_tui_repaint_status(void);

/* Newline aman di scroll region TUI (scroll up sebelum overflow footer). */
void agnc_tui_chat_newline(void);

/* Guard sebelum menulis baris chat (scroll region lock aktif). */
void agnc_tui_chat_before_write(void);

/* Bersihkan baris prompt (setelah Enter, sebelum chat). */
void agnc_tui_clear_input_row(void);

/* Sinkronkan + tulis chat via Win32 (hindari printf/VT desync). */
void agnc_tui_chat_write(const char *text);
void agnc_tui_chat_write_ln(const char *text);

/* Setelah printf ANSI ke stdout dalam mode chat TUI. */
void agnc_tui_chat_flush_stdio(void);

int agnc_tui_scroll_locked(void);

void agnc_tui_end_chat_output(void);

#endif /* AGNC_TUI_H */
