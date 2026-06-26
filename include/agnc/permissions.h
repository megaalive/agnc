/*
 * permissions.h
 *
 * Gate permission interaktif untuk tool berisiko (Fase 1: shell).
 */

#ifndef AGNC_PERMISSIONS_H
#define AGNC_PERMISSIONS_H

#include "agnc/status.h"

/* Tanya pengguna di stdin; *allowed = 1 jika diizinkan. */
agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed);

#endif
