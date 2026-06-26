/*
 * console.h
 *
 * Inisialisasi konsol (UTF-8, ANSI VT) dan helper cetak respons CLI.
 */

#ifndef AGNC_CONSOLE_H
#define AGNC_CONSOLE_H

#include <stddef.h>

void agnc_console_init(void);
int agnc_console_vt_enabled(void);

void agnc_console_format_time_hm(char *out, size_t out_cap);
void agnc_console_print_role_header(const char *role);
void agnc_console_print_timestamped_message(const char *text);
void agnc_console_print_assistant_body(const char *text); /* --print: body tanpa header role */

#endif
