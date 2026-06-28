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

/* Bersihkan layar (TUI atau ANSI cls). */
void agnc_console_clear_screen(void);

/* Mulai blok teks REPL di scroll region TUI. */
void agnc_console_begin_repl_output(void);

/* Akhiri blok; repaint chrome setelah output panjang selesai. */
void agnc_console_end_repl_output(void);

/* printf ke scroll region TUI (scroll up otomatis, hindari timpa footer). */
void agnc_console_repl_printf(const char *fmt, ...);

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
void agnc_console_print_chat_assistant_begin_routed(const char *routing_label);
void agnc_console_print_chat_system(const char *text);

/* Hentikan spinner lalu tampilkan prompt izin tool (REPL). */
void agnc_console_print_permission_prompt(const char *label, const char *detail);

/* Baris aktivitas tool di REPL (stdout, tanpa timestamp). */
void agnc_console_print_chat_tool(const char *text);

/* Spinner REPL saat menunggu respons model (thread latar). */
void agnc_console_spinner_start(void);
void agnc_console_spinner_stop(void);

/* Baca jawaban [y/N] dari konsol (hindari fgets setelah line editing Windows). */
void agnc_console_read_yes_no(int *allowed);

#ifdef _WIN32
/* Sesi input konsol mentah — satu API untuk REPL line edit dan prompt [y/N]. */
typedef struct agnc_console_input_session {
    void *in_handle;
    void *out_handle;
    unsigned long saved_mode;
    int active;
} agnc_console_input_session_t;

void agnc_console_input_begin(agnc_console_input_session_t *session);
void agnc_console_input_end(agnc_console_input_session_t *session);
void agnc_console_input_prepare_repl(void);
void agnc_console_input_echo_char(agnc_console_input_session_t *session, char ch);
void agnc_console_input_echo_backspace(agnc_console_input_session_t *session);
void agnc_console_input_echo_newline(agnc_console_input_session_t *session);
int agnc_console_input_key_printable(unsigned long control_state, unsigned short vk, char ascii_char);
int agnc_console_input_is_paste_key(unsigned long control_state, unsigned short vk, char ascii_char);
size_t agnc_console_input_paste_clipboard(char *dest, size_t capacity);
#endif

#endif /* AGNC_CONSOLE_H */
