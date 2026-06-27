/*
 * registry.h
 *
 * Multi-server MCP: connect server aktif dari config dan simpan tools/list.
 */

#ifndef AGNC_MCP_REGISTRY_H
#define AGNC_MCP_REGISTRY_H

#include "agnc/config.h"
#include "agnc/mcp/client.h"
#include "agnc/status.h"

#include <stddef.h>

typedef struct {
    char *server_id;
    char *tools_json;
    agnc_mcp_client_t client;
} agnc_mcp_connected_server_t;

typedef struct {
    agnc_mcp_connected_server_t *servers;
    size_t count;
} agnc_mcp_registry_t;

void agnc_mcp_registry_init(agnc_mcp_registry_t *registry);
void agnc_mcp_registry_free(agnc_mcp_registry_t *registry);

/*
 * Connect semua entri mcp.servers[] yang enabled=true.
 * Server yang gagal connect dilewati; status OK jika setidaknya satu berhasil atau tidak ada yang enabled.
 */
agnc_status_t agnc_mcp_registry_load_from_config(
    const agnc_config_t *config,
    agnc_mcp_registry_t *registry,
    unsigned timeout_ms);

size_t agnc_mcp_registry_server_count(const agnc_mcp_registry_t *registry);
const agnc_mcp_connected_server_t *agnc_mcp_registry_server_at(const agnc_mcp_registry_t *registry, size_t index);

#endif /* AGNC_MCP_REGISTRY_H */
