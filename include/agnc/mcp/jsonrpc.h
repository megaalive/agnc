/*
 * jsonrpc.h
 *
 * JSON-RPC 2.0 untuk transport MCP stdio (satu objek JSON per baris).
 */

#ifndef AGNC_MCP_JSONRPC_H
#define AGNC_MCP_JSONRPC_H

#include "agnc/status.h"

#include <stdint.h>

typedef struct {
    int is_request;
    int is_response;
    int is_notification;
    int has_id;
    int64_t id;
    char *method;
    char *params_json;
    char *result_json;
    int has_error;
    int error_code;
    char *error_message;
} agnc_jsonrpc_message_t;

void agnc_jsonrpc_message_init(agnc_jsonrpc_message_t *message);
void agnc_jsonrpc_message_free(agnc_jsonrpc_message_t *message);

/* Parse satu baris JSON-RPC; pemanggil wajib free message setelah pakai. */
agnc_status_t agnc_jsonrpc_parse_line(const char *json_line, agnc_jsonrpc_message_t *message);

/*
 * Bangun pesan JSON-RPC (string heap-owned, wajib free).
 * params_json / result_json boleh NULL; jika ada harus objek/array JSON valid.
 */
char *agnc_jsonrpc_format_request(int64_t id, const char *method, const char *params_json);
char *agnc_jsonrpc_format_notification(const char *method, const char *params_json);
char *agnc_jsonrpc_format_response_ok(int64_t id, const char *result_json);
char *agnc_jsonrpc_format_response_error(int64_t id, int code, const char *message);

#endif /* AGNC_MCP_JSONRPC_H */
