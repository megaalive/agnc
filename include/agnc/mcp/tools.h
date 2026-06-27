/*
 * tools.h
 *
 * Katalog tool MCP runtime: nama diekspos ke model dan schema OpenAI-compatible.
 */

#ifndef AGNC_MCP_TOOLS_H
#define AGNC_MCP_TOOLS_H

#include "agnc/mcp/registry.h"
#include "agnc/status.h"

#include <stddef.h>

typedef struct {
    char *exposed_name;
    char *mcp_tool_name;
    char *description;
    char *parameters_json;
    size_t server_index;
} agnc_mcp_runtime_tool_t;

typedef struct {
    agnc_mcp_runtime_tool_t *tools;
    size_t count;
} agnc_mcp_tool_catalog_t;

void agnc_mcp_tool_catalog_init(agnc_mcp_tool_catalog_t *catalog);
void agnc_mcp_tool_catalog_free(agnc_mcp_tool_catalog_t *catalog);

agnc_status_t agnc_mcp_tool_catalog_build(const agnc_mcp_registry_t *registry, agnc_mcp_tool_catalog_t *catalog);

const agnc_mcp_runtime_tool_t *agnc_mcp_tool_catalog_find(
    const agnc_mcp_tool_catalog_t *catalog,
    const char *exposed_name);

#endif /* AGNC_MCP_TOOLS_H */
