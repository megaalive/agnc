/*
 * console.h
 *
 * Inisialisasi konsol (UTF-8, ANSI VT) dan helper cetak respons CLI.
 */

#ifndef AGNC_CONSOLE_H
#define AGNC_CONSOLE_H

#include <stddef.h>

/* Palet REPL (VT-enabled). */
#define AGNC_ANSI_RESET "\033[0m"
#define AGNC_ANSI_DIM "\033[2m"
#define AGNC_ANSI_BOLD "\033[1m"
#define AGNC_ANSI_USER "\033[92m"  /* bright green — input pengguna */
#define AGNC_ANSI_CODE "\033[90m"  /* abu-abu — inline code / fence di jawaban model */

void agnc_console_init(void);
int agnc_console_vt_enabled(void);

void agnc_console_format_time_hm(char *out, size_t out_cap);
void agnc_console_format_time_hms(char *out, size_t out_cap);
void agnc_console_print_role_header(const char *role);
void agnc_console_print_timestamped_message(const char *text);
void agnc_console_print_assistant_body(const char *text); /* --print: body tanpa header role */

/* REPL: hapus baris input mentah (setelah Enter) agar tidak terduplikasi di log chat. */
void agnc_console_clear_input_line(void);

/* REPL chat blocks — timestamp di baris sendiri, lalu isi pesan. */
void agnc_console_print_chat_user(const char *text);
void agnc_console_print_chat_assistant_begin(void);
void agnc_console_print_chat_system(const char *text);

/* Hentikan spinner lalu tampilkan prompt izin tool (REPL). */
void agnc_console_print_permission_prompt(const char *label, const char *detail);

/* Baris aktivitas tool di REPL (stdout, tanpa timestamp). */
void agnc_console_print_chat_tool(const char *text);

/* Spinner REPL saat menunggu respons model (thread latar). */
void agnc_console_spinner_start(void);
void agnc_console_spinner_stop(void);

#endif /* AGNC_CONSOLE_H */
