/*
 * session.h
 *
 * Persistensi riwayat percakapan ke ~/.agnc/sessions (JSON).
 */

#ifndef AGNC_SESSION_H
#define AGNC_SESSION_H

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/status.h"

/* Path folder sesi default (~/.agnc/sessions). */
agnc_status_t agnc_session_default_dir(char **output);

/* Path file sesi aktif (~/.agnc/sessions/current.json) — alias nama "current". */
agnc_status_t agnc_session_current_path(char **output);

/* Validasi nama sesi: huruf, angka, _, - ; panjang 1–48. */
agnc_status_t agnc_session_validate_name(const char *name);

/* Path file JSON untuk nama sesi (current → current.json, lain → <name>.json). */
agnc_status_t agnc_session_path_for_name(const char *name, char **output);

/* Muat/simpan pointer sesi aktif (~/.agnc/sessions/active.txt). */
agnc_status_t agnc_session_active_name_load(char **name_out);
agnc_status_t agnc_session_active_name_save(const char *name);

/* Daftar nama sesi (*.json di folder, tanpa file temp). Pemanggil free tiap string + array. */
agnc_status_t agnc_session_list_names(const char *sessions_dir, char ***names_out, size_t *count_out);
void agnc_session_list_names_free(char **names, size_t count);

/*
 * Hapus file current.json.tmp* yang tertinggal setelah atomic write terputus.
 * Dipanggil saat REPL startup. Aman di unit test via sessions_dir eksplisit.
 */
agnc_status_t agnc_session_cleanup_stale_temp_files(void);

agnc_status_t agnc_session_cleanup_stale_temp_files_in_dir(const char *sessions_dir);

agnc_status_t agnc_session_load(
    const char *path,
    agnc_conversation_t *conversation,
    char **provider_id_out,
    char **model_out);

agnc_status_t agnc_session_save(
    const char *path,
    const agnc_conversation_t *conversation,
    const agnc_config_t *config);

#endif /* AGNC_SESSION_H */
