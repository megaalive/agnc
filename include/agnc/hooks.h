/*
 * hooks.h
 *
 * Jalankan script shell pada event agent (Fase 6.14).
 */

#ifndef AGNC_HOOKS_H
#define AGNC_HOOKS_H

#include "agnc/config.h"
#include "agnc/status.h"

typedef enum {
    AGNC_HOOK_EVENT_SESSION_START = 0,
    AGNC_HOOK_EVENT_PRE_TURN,
    AGNC_HOOK_EVENT_POST_TURN,
    AGNC_HOOK_EVENT_PRE_TOOL,
    AGNC_HOOK_EVENT_POST_TOOL,
    AGNC_HOOK_EVENT_COUNT
} agnc_hook_event_id_t;

typedef struct {
    const char *session_name;
    const char *user_prompt;
    const char *tool_name;
    const char *tool_arguments;
    const char *tool_status;
    const char *provider_id;
    const char *model;
    long usage_prompt;
    long usage_completion;
    long usage_total;
} agnc_hook_payload_input_t;

const char *agnc_hooks_event_name(agnc_hook_event_id_t event_id);

char *agnc_hooks_build_payload_json(agnc_hook_event_id_t event_id, const agnc_hook_payload_input_t *input);

void agnc_hooks_free_payload(char *payload_json);

/*
 * Jalankan semua hook untuk event.
 * Payload JSON ditulis ke file sementara; env AGNC_HOOK_EVENT dan AGNC_HOOK_PAYLOAD_FILE diset.
 * pre_tool: exit code non-nol memblokir eksekusi tool (return TOOL_DENIED).
 * Lainnya: kegagalan hook dicatat ke stderr jika verbose; tidak menggagalkan turn.
 */
agnc_status_t agnc_hooks_run(
    const agnc_config_t *config,
    agnc_hook_event_id_t event_id,
    const char *payload_json,
    int *blocked_out);

size_t agnc_hooks_count_for_event(const agnc_config_t *config, agnc_hook_event_id_t event_id);

#endif /* AGNC_HOOKS_H */
