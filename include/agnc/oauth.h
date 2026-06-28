/*
 * oauth.h
 *
 * Penyimpanan token OAuth/API di ~/.agnc/oauth/<provider_id>.json.
 */

#ifndef AGNC_OAUTH_H
#define AGNC_OAUTH_H

#include "agnc/status.h"

#define AGNC_OAUTH_ADVISORY_REFRESH_SECONDS 120
#define AGNC_OAUTH_MANDATORY_REFRESH_SECONDS 30

typedef struct {
    char *access_token;
    char *refresh_token;
    long expires_at;
} agnc_oauth_token_t;

void agnc_oauth_token_init(agnc_oauth_token_t *token);
void agnc_oauth_token_free(agnc_oauth_token_t *token);

agnc_status_t agnc_oauth_token_path(const char *provider_id, char **path_out);

agnc_status_t agnc_oauth_token_load(const char *provider_id, agnc_oauth_token_t *token_out);

agnc_status_t agnc_oauth_token_save(const char *provider_id, const agnc_oauth_token_t *token);

agnc_status_t agnc_oauth_token_delete(const char *provider_id);

/*
 * Detik hingga kadaluarsa; -1 jika expires_at tidak diset; negatif jika sudah lewat.
 */
long agnc_oauth_token_seconds_until_expiry_at(const agnc_oauth_token_t *token, long now_unix);

/*
 * Perlu refresh jika ada refresh_token dan (force || expires_at <= now + threshold).
 * Tanpa expires_at tidak auto-refresh (token manual via oauth set).
 */
int agnc_oauth_token_needs_refresh_at(const agnc_oauth_token_t *token, long now_unix, int force);

/*
 * Parse respons POST /v1/oauth/token; pertahankan refresh_token lama jika tidak di respons.
 */
agnc_status_t agnc_oauth_parse_refresh_response(
    const char *json_body,
    const agnc_oauth_token_t *previous,
    agnc_oauth_token_t *updated_out,
    long now_unix);

/*
 * Refresh token di disk jika perlu (atau force). Hanya provider yang didukung (anthropic).
 */
agnc_status_t agnc_oauth_refresh_if_needed(const char *provider_id, int force);

/*
 * Muat token, refresh jika perlu, kembalikan access_token owned di *access_out.
 */
agnc_status_t agnc_oauth_load_fresh_access_token(const char *provider_id, char **access_out);

/*
 * Kesehatan token untuk doctor: -1 tidak ada, 0 ok, 1 hampir expired, 2 expired.
 */
int agnc_oauth_token_health(const char *provider_id, char *detail, size_t detail_cap);

/* CLI: agnc oauth set|status|clear|refresh */
int agnc_cli_run_oauth(int argc, char **argv);

#endif /* AGNC_OAUTH_H */
