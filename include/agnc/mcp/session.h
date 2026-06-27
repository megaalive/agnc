/*
 * session.h
 *
 * Lifetime koneksi MCP per sesi REPL (hindari respawn npx tiap prompt).
 */

#ifndef AGNC_MCP_SESSION_H
#define AGNC_MCP_SESSION_H

#include "agnc/config.h"
#include "agnc/mcp/registry.h"
#include "agnc/mcp/tools.h"
#include "agnc/status.h"

typedef struct {
    agnc_mcp_registry_t registry;
    agnc_mcp_tool_catalog_t catalog;
    int loaded;
} agnc_mcp_session_t;

void agnc_mcp_session_init(agnc_mcp_session_t *session);
void agnc_mcp_session_reset(agnc_mcp_session_t *session);
void agnc_mcp_session_free(agnc_mcp_session_t *session);

/* Muat ulang hanya jika belum loaded; panggil reset setelah config berubah. */
agnc_status_t agnc_mcp_session_ensure(agnc_mcp_session_t *session, const agnc_config_t *config, unsigned timeout_ms);

/* Putuskan semua koneksi lalu muat ulang dari config. */
agnc_status_t agnc_mcp_session_reconnect(agnc_mcp_session_t *session, const agnc_config_t *config, unsigned timeout_ms);

#endif /* AGNC_MCP_SESSION_H */
