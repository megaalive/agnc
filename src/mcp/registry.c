/*
 * registry.c
 *
 * Manager multi-server MCP dari config.
 */

#include "agnc/mcp/registry.h"

#include <stdlib.h>
#include <string.h>

static char *agnc_mcp_registry_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_mcp_registry_init(agnc_mcp_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }

    memset(registry, 0, sizeof(*registry));
}

static void agnc_mcp_registry_free_server(agnc_mcp_connected_server_t *server)
{
    if (server == NULL) {
        return;
    }

    free(server->server_id);
    free(server->tools_json);
    agnc_mcp_client_close(&server->client);
    server->server_id = NULL;
    server->tools_json = NULL;
}

void agnc_mcp_registry_free(agnc_mcp_registry_t *registry)
{
    size_t index;

    if (registry == NULL) {
        return;
    }

    for (index = 0; index < registry->count; index++) {
        agnc_mcp_registry_free_server(&registry->servers[index]);
    }

    free(registry->servers);
    registry->servers = NULL;
    registry->count = 0;
}

agnc_status_t agnc_mcp_registry_load_from_config(
    const agnc_config_t *config,
    agnc_mcp_registry_t *registry,
    unsigned timeout_ms)
{
    size_t index;
    size_t enabled_count = 0;
    size_t connected_count = 0;
    agnc_mcp_connected_server_t *connected;
    agnc_status_t status;

    if (config == NULL || registry == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_mcp_registry_free(registry);
    agnc_mcp_registry_init(registry);

    if (config->mcp_servers == NULL || config->mcp_server_count == 0) {
        return AGNC_STATUS_OK;
    }

    for (index = 0; index < config->mcp_server_count; index++) {
        if (config->mcp_servers[index].enabled) {
            enabled_count++;
        }
    }

    if (enabled_count == 0) {
        return AGNC_STATUS_OK;
    }

    connected = (agnc_mcp_connected_server_t *)calloc(enabled_count, sizeof(*connected));
    if (connected == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < config->mcp_server_count; index++) {
        const agnc_mcp_server_config_t *server_config = &config->mcp_servers[index];
        agnc_mcp_connected_server_t *entry;
        char *tools_json = NULL;

        if (!server_config->enabled) {
            continue;
        }

        if (server_config->command == NULL || server_config->command[0] == '\0') {
            continue;
        }

        entry = &connected[connected_count];
        agnc_mcp_client_init(&entry->client);

        status = agnc_mcp_client_connect(
            server_config->command,
            (const char *const *)server_config->args,
            server_config->arg_count,
            server_config->cwd,
            (const char *const *)server_config->env_keys,
            (const char *const *)server_config->env_values,
            server_config->env_count,
            &entry->client,
            &tools_json,
            timeout_ms);
        if (status != AGNC_STATUS_OK) {
            agnc_mcp_client_close(&entry->client);
            continue;
        }

        entry->server_id = agnc_mcp_registry_strdup_local(
            server_config->id != NULL ? server_config->id : "mcp");
        entry->tools_json = tools_json;

        if (entry->server_id == NULL) {
            free(tools_json);
            agnc_mcp_client_close(&entry->client);
            continue;
        }

        connected_count++;
    }

    if (connected_count == 0) {
        free(connected);
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    registry->servers = connected;
    registry->count = connected_count;
    return AGNC_STATUS_OK;
}

size_t agnc_mcp_registry_server_count(const agnc_mcp_registry_t *registry)
{
    if (registry == NULL) {
        return 0;
    }

    return registry->count;
}

const agnc_mcp_connected_server_t *agnc_mcp_registry_server_at(const agnc_mcp_registry_t *registry, size_t index)
{
    if (registry == NULL || index >= registry->count) {
        return NULL;
    }

    return &registry->servers[index];
}
