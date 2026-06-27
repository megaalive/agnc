/*
 * opencode.h
 *
 * Client native OpenCode serve (OpenAPI session + message).
 */

#ifndef AGNC_OPENCODE_H
#define AGNC_OPENCODE_H

#include "agnc/config.h"
#include "agnc/net/sse.h"
#include "agnc/status.h"

#define AGNC_OPENCODE_DEFAULT_BASE_URL "http://127.0.0.1:4096"

agnc_status_t agnc_opencode_probe(
    const char *base_url,
    char *detail,
    size_t detail_size);

agnc_status_t agnc_opencode_list_models(
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count,
    volatile int *cancel_flag);

/*
 * Parse "providerID/modelID" ke komponen terpisah.
 * Jika tidak ada '/', providerID = "opencode", modelID = input.
 */
agnc_status_t agnc_opencode_parse_model(
    const char *model,
    char **provider_id_out,
    char **model_id_out);

agnc_status_t agnc_opencode_run_turn(
    const agnc_config_t *config,
    const char *session_sqlite_path,
    const char *system_prompt,
    const char *user_text,
    agnc_sse_parser_t *parser,
    char **error_message,
    volatile int *cancel_flag);

void agnc_opencode_clear_session_link(const char *session_sqlite_path);

#endif /* AGNC_OPENCODE_H */
