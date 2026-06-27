/*
 * client.h
 *
 * Handshake MCP minimal (initialize + tools/list) di atas transport stdio.
 */

#ifndef AGNC_MCP_CLIENT_H
#define AGNC_MCP_CLIENT_H

#include "agnc/mcp/stdio.h"
#include "agnc/status.h"

typedef struct {
    agnc_mcp_stdio_conn_t *transport;
    int initialized;
} agnc_mcp_client_t;

void agnc_mcp_client_init(agnc_mcp_client_t *client);
void agnc_mcp_client_close(agnc_mcp_client_t *client);

/*
 * Connect + handshake MCP. tools_json_out berisi array tools dari tools/list (heap-owned).
 */
agnc_status_t agnc_mcp_client_connect(
    const char *command,
    const char *const *argv_extra,
    size_t argc,
    const char *cwd,
    const char *const *env_keys,
    const char *const *env_values,
    size_t env_count,
    agnc_mcp_client_t *client,
    char **tools_json_out,
    unsigned timeout_ms);

agnc_status_t agnc_mcp_client_call_tool(
    agnc_mcp_client_t *client,
    const char *tool_name,
    const char *arguments_json,
    char **result_text_out,
    unsigned timeout_ms);

#endif /* AGNC_MCP_CLIENT_H */
