/*
 * path.h
 *
 * Utilitas path cross-platform untuk agnc.
 * Semua fungsi yang menulis ke `char **output` mengalokasikan memory
 * yang wajib dibebaskan oleh pemanggil dengan free().
 */

#ifndef AGNC_PATH_H
#define AGNC_PATH_H

#include "agnc/status.h"

/*
 * Ekspansi ~ ke home directory.
 * Input:  "~/.agnc/sessions/foo"
 * Output: "C:\\Users\\...\\.agnc\\sessions\\foo" (Windows)
 */
agnc_status_t agnc_path_expand_user(const char *input, char **output);

/*
 * Path default file config global (~/.agnc.json setelah diekspansi).
 */
agnc_status_t agnc_path_default_config(char **output);

/*
 * Cek keberadaan path di filesystem.
 * Return 1 jika ada, 0 jika tidak.
 */
int agnc_path_exists(const char *path);

#endif
