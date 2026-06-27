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

/* Muat/simpan sesi SQLite; migrasi otomatis dari .json legacy saat load. */
agnc_status_t agnc_session_load(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
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

/* Ringkas DB + meta summary; muat ulang tail ke conversation. */
agnc_status_t agnc_session_compact_storage(
    const char *path,
    agnc_conversation_t *conversation,
    const agnc_config_t *config,
    size_t keep_tail_messages);

#endif /* AGNC_SESSION_H */
