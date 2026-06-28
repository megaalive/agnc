/*
 * line_edit.h
 *
 * Input baris REPL dengan cursor, backspace, dan history ringkas.
 */

#ifndef AGNC_LINE_EDIT_H
#define AGNC_LINE_EDIT_H

#include <stddef.h>

/* Baca satu baris dari stdin; mengembalikan 0 jika EOF atau Ctrl+C (keluar REPL). */
int agnc_repl_read_line(char *buffer, size_t capacity);

void agnc_repl_line_reset_exit(void);
void agnc_repl_line_signal_exit(void);

typedef void (*agnc_repl_line_idle_fn)(void);
typedef int (*agnc_repl_line_idle_poll_fn)(void);
typedef int (*agnc_repl_line_idle_needed_fn)(void);

/*
 * Hook polling job background saat menunggu input (tanpa mengganggu sesi raw mode).
 * poll: return 1 jika output konsol berubah (perlu reset prompt `>`).
 */
void agnc_repl_line_set_idle(
    agnc_repl_line_idle_needed_fn needed,
    agnc_repl_line_idle_poll_fn poll,
    agnc_repl_line_idle_needed_fn perm_needed,
    agnc_repl_line_idle_fn perm_handle);

#endif /* AGNC_LINE_EDIT_H */
