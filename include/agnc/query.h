/*
 * query.h
 *
 * Agent loop Fase 1: kirim prompt ke provider, stream jawaban,
 * eksekusi tool read_file jika diminta model, ulangi sampai selesai.
 */

#ifndef AGNC_QUERY_H
#define AGNC_QUERY_H

#include "agnc/config.h"
#include "agnc/status.h"

/* Jalankan satu sesi headless --print dengan prompt pengguna. */
agnc_status_t agnc_query_print(const agnc_config_t *config, const char *prompt);

#endif
