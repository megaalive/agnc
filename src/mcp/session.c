/*
 * session.c
 *
 * Persist koneksi MCP untuk sesi REPL.
 */

#include "agnc/mcp/session.h"

void agnc_mcp_session_init(agnc_mcp_session_t *session)
{
    if (session == NULL) {
        return;
    }

    agnc_mcp_registry_init(&session->registry);
    agnc_mcp_tool_catalog_init(&session->catalog);
    session->loaded = 0;
}

void agnc_mcp_session_reset(agnc_mcp_session_t *session)
{
    if (session == NULL) {
        return;
    }

    agnc_mcp_tool_catalog_free(&session->catalog);
    agnc_mcp_registry_free(&session->registry);
    agnc_mcp_session_init(session);
}

void agnc_mcp_session_free(agnc_mcp_session_t *session)
{
    if (session == NULL) {
        return;
    }

    agnc_mcp_session_reset(session);
}

agnc_status_t agnc_mcp_session_ensure(agnc_mcp_session_t *session, const agnc_config_t *config, unsigned timeout_ms)
{
    agnc_status_t status;

    if (session == NULL || config == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (session->loaded) {
        return AGNC_STATUS_OK;
    }

    if (config->mcp_server_count == 0) {
        session->loaded = 1;
        return AGNC_STATUS_OK;
    }

    status = agnc_mcp_registry_load_from_config(config, &session->registry, timeout_ms);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_mcp_tool_catalog_build(&session->registry, &session->catalog);
    if (status != AGNC_STATUS_OK) {
        agnc_mcp_registry_free(&session->registry);
        agnc_mcp_registry_init(&session->registry);
        return status;
    }

    session->loaded = 1;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_mcp_session_reconnect(agnc_mcp_session_t *session, const agnc_config_t *config, unsigned timeout_ms)
{
    if (session == NULL || config == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_mcp_session_reset(session);
    return agnc_mcp_session_ensure(session, config, timeout_ms);
}
