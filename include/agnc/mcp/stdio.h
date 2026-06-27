/*
 * stdio.h
 *
 * Transport MCP: spawn proses child dan bingkai JSON-RPC per baris (stdin/stdout).
 */

#ifndef AGNC_MCP_STDIO_H
#define AGNC_MCP_STDIO_H

#include "agnc/mcp/jsonrpc.h"
#include "agnc/status.h"

#include <stddef.h>
#include <stdint.h>

typedef struct agnc_mcp_stdio_conn agnc_mcp_stdio_conn_t;

/*
 * Spawn server MCP (command + argumen terpisah).
 * argv_extra[0..argc-1] adalah argumen setelah executable; boleh NULL jika argc=0.
 */
agnc_status_t agnc_mcp_stdio_spawn(
    const char *command,
    const char *const *argv_extra,
    size_t argc,
    const char *cwd,
    agnc_mcp_stdio_conn_t **conn_out);

void agnc_mcp_stdio_close(agnc_mcp_stdio_conn_t *conn);

agnc_status_t agnc_mcp_stdio_write_line(agnc_mcp_stdio_conn_t *conn, const char *json_line);

/*
 * Baca satu pesan JSON-RPC dari stdout child (abaikan baris kosong).
 * timeout_ms=0 menunggu tanpa batas.
 */
agnc_status_t agnc_mcp_stdio_read_message(
    agnc_mcp_stdio_conn_t *conn,
    agnc_jsonrpc_message_t *message,
    unsigned timeout_ms);

/* Kirim request, tunggu respons dengan id yang sama. */
agnc_status_t agnc_mcp_stdio_call(
    agnc_mcp_stdio_conn_t *conn,
    const char *method,
    const char *params_json,
    agnc_jsonrpc_message_t *response,
    unsigned timeout_ms);

agnc_status_t agnc_mcp_stdio_notify(
    agnc_mcp_stdio_conn_t *conn,
    const char *method,
    const char *params_json);

#endif /* AGNC_MCP_STDIO_H */
