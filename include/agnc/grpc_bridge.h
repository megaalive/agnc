/*
 * grpc_bridge.h
 *
 * Bridge C murni dari server gRPC ke agnc_query_run (headless, tanpa stdout).
 */

#ifndef AGNC_GRPC_BRIDGE_H
#define AGNC_GRPC_BRIDGE_H

#include "agnc/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*agnc_grpc_stream_delta_fn)(const char *text, size_t length, void *user_data);

/* Return 1 = izinkan, 0 = tolak. Dipanggil saat auto_approve=0 dan tool butuh izin. */
typedef int (*agnc_grpc_permission_ask_fn)(const char *kind, const char *detail, void *user_data);

typedef struct {
    const char *prompt;
    const char *session_name;
    int auto_approve;
    int enable_tools;
    volatile int *cancel_flag;
    agnc_grpc_stream_delta_fn stream_delta;
    void *stream_delta_ctx;
    agnc_grpc_permission_ask_fn permission_ask;
    void *permission_ask_ctx;
} agnc_grpc_query_request_t;

typedef struct {
    agnc_status_t status;
    char *assistant_text;
    char *error_message;
    long usage_prompt_tokens;
    long usage_completion_tokens;
    long usage_total_tokens;
} agnc_grpc_query_result_t;

void agnc_grpc_query_result_init(agnc_grpc_query_result_t *result);
void agnc_grpc_query_result_free(agnc_grpc_query_result_t *result);

agnc_status_t agnc_grpc_run_query(
    const agnc_grpc_query_request_t *request,
    agnc_grpc_query_result_t *result);

const char *agnc_grpc_status_label(agnc_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* AGNC_GRPC_BRIDGE_H */
