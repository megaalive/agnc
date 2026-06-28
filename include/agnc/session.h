/*
 * session.h
 *
 * Persistensi riwayat percakapan ke ~/.agnc/sessions/<nama>.sqlite.
 */

#ifndef AGNC_SESSION_H
#define AGNC_SESSION_H

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/status.h"

/* Akumulasi token usage tersimpan di meta sesi SQLite. */
typedef struct {
    long prompt_tokens;
    long completion_tokens;
    long total_tokens;
} agnc_session_usage_t;

/* Path folder sesi default (~/.agnc/sessions). */
agnc_status_t agnc_session_default_dir(char **output);

/* Path file sesi aktif (~/.agnc/sessions/current.sqlite) — alias nama "current". */
agnc_status_t agnc_session_current_path(char **output);

/* Validasi nama sesi: huruf, angka, _, - ; panjang 1–48. */
agnc_status_t agnc_session_validate_name(const char *name);

/* Path file SQLite untuk nama sesi (current → current.sqlite, lain → <name>.sqlite). */
agnc_status_t agnc_session_path_for_name(const char *name, char **output);

/* Hapus file sesi (.sqlite dan .json legacy); IO_ERROR jika tidak ada. */
agnc_status_t agnc_session_delete_by_name(const char *name);

/* Muat/simpan pointer sesi aktif (~/.agnc/sessions/active.txt). */
agnc_status_t agnc_session_active_name_load(char **name_out);
agnc_status_t agnc_session_active_name_save(const char *name);

/* Daftar nama sesi (*.sqlite + .json legacy tanpa pasangan sqlite). */
agnc_status_t agnc_session_list_names(const char *sessions_dir, char ***names_out, size_t *count_out);
void agnc_session_list_names_free(char **names, size_t count);

/*
 * Hapus file temp (*.sqlite.tmp*, *.json.tmp*) yang tertinggal.
 * Dipanggil saat REPL startup.
 */
agnc_status_t agnc_session_cleanup_stale_temp_files(void);

agnc_status_t agnc_session_cleanup_stale_temp_files_in_dir(const char *sessions_dir);

agnc_status_t agnc_session_cleanup_legacy_bg_files(void);
agnc_status_t agnc_session_cleanup_legacy_bg_files_in_dir(const char *sessions_dir);

/* Muat/simpan sesi SQLite; migrasi otomatis dari .json legacy saat load. */
agnc_status_t agnc_session_load(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out);

/* Hint routing terakhir di meta sesi (last_*; fallback ke kunci legacy). */
agnc_status_t agnc_session_load_routing_hint(
    const char *path,
    char **provider_id_out,
    char **gateway_id_out,
    char **model_out);

/* Append pesan baru (unsynced) + meta; menggantikan full rewrite. */
agnc_status_t agnc_session_sync(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config);

agnc_status_t agnc_session_save(
    const char *path,
    const agnc_conversation_t *conversation,
    const agnc_config_t *config);

/* Hapus semua pesan di DB sesi (mis. /clear). */
agnc_status_t agnc_session_clear_messages(const char *path, const agnc_config_t *config);

/* Ringkas RAM saja; riwayat di SQLite tidak dihapus. path diabaikan. */
agnc_status_t agnc_session_compact_history(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config,
    size_t keep_tail_messages);

/* 1 jika config + ukuran sesi memenuhi syarat auto-compact. */
int agnc_session_should_auto_compact(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    const agnc_session_usage_t *usage);

/*
 * Jalankan compact bila perlu; set *did_compact=1 jika riwayat diringkas.
 * path boleh NULL (hanya RAM).
 */
agnc_status_t agnc_session_auto_compact_if_needed(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config,
    const agnc_session_usage_t *usage,
    int *did_compact);

void agnc_session_usage_init(agnc_session_usage_t *usage);

agnc_status_t agnc_session_usage_load(const char *path, agnc_session_usage_t *usage_out);

agnc_status_t agnc_session_usage_save(const char *path, const agnc_session_usage_t *usage);

/* Tambahkan delta turn ke total sesi (nilai < 0 diabaikan). */
agnc_status_t agnc_session_usage_accumulate(
    const char *path,
    long prompt_delta,
    long completion_delta,
    long total_delta);

agnc_status_t agnc_session_usage_reset(const char *path);

/* Meta key/value di tabel meta sesi SQLite (mis. opencode_session_id). */
agnc_status_t agnc_session_meta_get(const char *path, const char *key, char **value_out);
agnc_status_t agnc_session_meta_set(const char *path, const char *key, const char *value);
agnc_status_t agnc_session_meta_delete(const char *path, const char *key);

agnc_status_t agnc_session_cost_load(const char *path, double *total_usd_out);
agnc_status_t agnc_session_cost_accumulate(const char *path, double delta_usd);
agnc_status_t agnc_session_cost_reset(const char *path);

/* --- Background jobs (schema v2, satu file sesi induk) --- */

/* Id pesan foreground terakhir (0 jika kosong). */
agnc_status_t agnc_session_foreground_max_id(const char *path, int64_t *max_id_out);

/*
 * Muat konteks foreground sampai max_id (inklusif) ke RAM untuk job bg.
 * max_id <= 0 = tanpa batas atas.
 */
agnc_status_t agnc_session_load_bg_context(
    const char *path,
    agnc_conversation_t *conversation,
    int64_t max_id,
    size_t tail_limit);

agnc_status_t agnc_session_bg_job_create(
    const char *path,
    unsigned job_id,
    const char *prompt,
    int64_t context_parent_id);

agnc_status_t agnc_session_bg_job_set_status(
    const char *path,
    unsigned job_id,
    const char *status,
    const char *summary,
    const char *error);

/* Sisipkan baris ringkas [bg #N] ke riwayat foreground sesi induk. */
agnc_status_t agnc_session_bg_append_foreground_notice(
    const char *path,
    unsigned job_id,
    const char *prompt,
    const char *summary,
    const agnc_config_t *config);

#endif /* AGNC_SESSION_H */
