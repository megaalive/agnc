/*
 * line_edit.h
 *
 * Input baris REPL dengan backspace dan history ringkas.
 */

#ifndef AGNC_LINE_EDIT_H
#define AGNC_LINE_EDIT_H

#include <stddef.h>

/* Baca satu baris dari stdin; mengembalikan 0 jika EOF, 1 jika OK. */
int agnc_repl_read_line(char *buffer, size_t capacity);

#endif /* AGNC_LINE_EDIT_H */
