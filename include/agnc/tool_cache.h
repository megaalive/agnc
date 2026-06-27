/*
 * tool_cache.h
 *
 * Cache in-memory hasil tool read-only per sesi REPL (grep, glob, read_file, find_symbol).
 */

#ifndef AGNC_TOOL_CACHE_H
#define AGNC_TOOL_CACHE_H

#include "agnc/status.h"

/* Kosongkan cache (awal sesi REPL, setelah write/edit). */
void agnc_tool_cache_reset(void);

/* Return 1 jika tool boleh di-cache. */
int agnc_tool_cache_is_eligible(const char *tool_name);

/*
 * Ambil salinan hasil cache; return 1 jika hit.
 * Pemanggil free(*result_out).
 */
int agnc_tool_cache_get(const char *tool_name, const char *arguments_json, char **result_out);

/* Simpan salinan hasil; abaikan jika result NULL. */
agnc_status_t agnc_tool_cache_put(const char *tool_name, const char *arguments_json, const char *result);

#endif
