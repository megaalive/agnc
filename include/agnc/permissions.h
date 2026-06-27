/*
 * permissions.h
 *
 * Gate permission interaktif untuk tool berisiko (shell, tulis/edit file).
 */

#ifndef AGNC_PERMISSIONS_H
#define AGNC_PERMISSIONS_H

#include "agnc/status.h"

/* Tanya pengguna di stdin; *allowed = 1 jika diizinkan. */
agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed);

/* Prompt tulis/edit file; operation = "write" atau "edit". */
agnc_status_t agnc_permission_ask_file_write(const char *path, const char *operation, int *allowed);

#endif
