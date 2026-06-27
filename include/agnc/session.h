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

/* Path file sesi aktif (~/.agnc/sessions/current.json). */
agnc_status_t agnc_session_current_path(char **output);

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
