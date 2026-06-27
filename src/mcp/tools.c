/*
 * tools.c
 *
 * Bangun katalog tool MCP untuk agent loop (prefix mcp_<id>_<tool>).
 */

#include "agnc/mcp/tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_MCP_EMPTY_PARAMETERS "{\"type\":\"object\",\"properties\":{}}"

static char *agnc_mcp_tools_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static char *agnc_mcp_tools_normalize_parameters(const char *input_schema_json)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *type_value;
    char *output;

    if (input_schema_json == NULL || input_schema_json[0] == '\0') {
        return agnc_mcp_tools_strdup_local(AGNC_MCP_EMPTY_PARAMETERS);
    }

    doc = yyjson_read(input_schema_json, strlen(input_schema_json), 0);
    if (doc == NULL) {
        return agnc_mcp_tools_strdup_local(AGNC_MCP_EMPTY_PARAMETERS);
    }

    root = yyjson_doc_get_root(doc);
    type_value = yyjson_obj_get(root, "type");
    if (type_value == NULL || !yyjson_is_str(type_value) || strcmp(yyjson_get_str(type_value), "object") != 0) {
        yyjson_doc_free(doc);
        return agnc_mcp_tools_strdup_local(AGNC_MCP_EMPTY_PARAMETERS);
    }

    output = yyjson_val_write(root, 0, NULL);
    yyjson_doc_free(doc);
    if (output == NULL) {
        return agnc_mcp_tools_strdup_local(AGNC_MCP_EMPTY_PARAMETERS);
    }

    return output;
}

static agnc_status_t agnc_mcp_tools_build_exposed_name(
    const char *server_id,
    const char *tool_name,
    char **exposed_name_out)
{
    size_t length;

    if (server_id == NULL || tool_name == NULL || exposed_name_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *exposed_name_out = NULL;
    length = strlen("mcp_") + strlen(server_id) + 1 + strlen(tool_name) + 1;
    *exposed_name_out = (char *)malloc(length);
    if (*exposed_name_out == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    snprintf(*exposed_name_out, length, "mcp_%s_%s", server_id, tool_name);
    return AGNC_STATUS_OK;
}

void agnc_mcp_tool_catalog_init(agnc_mcp_tool_catalog_t *catalog)
{
    if (catalog == NULL) {
        return;
    }

    memset(catalog, 0, sizeof(*catalog));
}

void agnc_mcp_tool_catalog_free(agnc_mcp_tool_catalog_t *catalog)
{
    size_t index;

    if (catalog == NULL) {
        return;
    }

    for (index = 0; index < catalog->count; index++) {
        free(catalog->tools[index].exposed_name);
        free(catalog->tools[index].mcp_tool_name);
        free(catalog->tools[index].description);
        free(catalog->tools[index].parameters_json);
    }

    free(catalog->tools);
    catalog->tools = NULL;
    catalog->count = 0;
}

agnc_status_t agnc_mcp_tool_catalog_build(const agnc_mcp_registry_t *registry, agnc_mcp_tool_catalog_t *catalog)
{
    size_t server_index;
    size_t total_tools = 0;
    size_t catalog_index = 0;
    agnc_mcp_runtime_tool_t *tools;

    if (registry == NULL || catalog == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_mcp_tool_catalog_free(catalog);
    agnc_mcp_tool_catalog_init(catalog);

    for (server_index = 0; server_index < agnc_mcp_registry_server_count(registry); server_index++) {
        const agnc_mcp_connected_server_t *server = agnc_mcp_registry_server_at(registry, server_index);
        yyjson_doc *doc;
        yyjson_val *tools_array;

        if (server == NULL || server->tools_json == NULL) {
            continue;
        }

        doc = yyjson_read(server->tools_json, strlen(server->tools_json), 0);
        if (doc == NULL) {
            continue;
        }

        tools_array = yyjson_doc_get_root(doc);
        if (!yyjson_is_arr(tools_array)) {
            yyjson_doc_free(doc);
            continue;
        }

        total_tools += yyjson_arr_size(tools_array);
        yyjson_doc_free(doc);
    }

    if (total_tools == 0) {
        return AGNC_STATUS_OK;
    }

    tools = (agnc_mcp_runtime_tool_t *)calloc(total_tools, sizeof(*tools));
    if (tools == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (server_index = 0; server_index < agnc_mcp_registry_server_count(registry); server_index++) {
        const agnc_mcp_connected_server_t *server = agnc_mcp_registry_server_at(registry, server_index);
        yyjson_doc *doc;
        yyjson_val *tools_array;
        size_t tool_index;
        size_t tool_count;

        if (server == NULL || server->tools_json == NULL || server->server_id == NULL) {
            continue;
        }

        doc = yyjson_read(server->tools_json, strlen(server->tools_json), 0);
        if (doc == NULL) {
            continue;
        }

        tools_array = yyjson_doc_get_root(doc);
        if (!yyjson_is_arr(tools_array)) {
            yyjson_doc_free(doc);
            continue;
        }

        tool_count = yyjson_arr_size(tools_array);
        for (tool_index = 0; tool_index < tool_count; tool_index++) {
            yyjson_val *entry = yyjson_arr_get(tools_array, tool_index);
            yyjson_val *name_value;
            yyjson_val *description_value;
            yyjson_val *schema_value;
            const char *tool_name;
            const char *description;
            char *schema_text = NULL;
            agnc_mcp_runtime_tool_t *runtime_tool;
            agnc_status_t status;

            if (entry == NULL || !yyjson_is_obj(entry)) {
                continue;
            }

            name_value = yyjson_obj_get(entry, "name");
            if (name_value == NULL || !yyjson_is_str(name_value)) {
                continue;
            }

            tool_name = yyjson_get_str(name_value);
            description_value = yyjson_obj_get(entry, "description");
            description = description_value != NULL && yyjson_is_str(description_value)
                ? yyjson_get_str(description_value)
                : "";

            schema_value = yyjson_obj_get(entry, "inputSchema");
            if (schema_value != NULL) {
                schema_text = yyjson_val_write(schema_value, 0, NULL);
            }

            runtime_tool = &tools[catalog_index];
            status = agnc_mcp_tools_build_exposed_name(server->server_id, tool_name, &runtime_tool->exposed_name);
            if (status != AGNC_STATUS_OK) {
                free(schema_text);
                agnc_mcp_tool_catalog_free(catalog);
                free(tools);
                return status;
            }

            runtime_tool->mcp_tool_name = agnc_mcp_tools_strdup_local(tool_name);
            runtime_tool->description = agnc_mcp_tools_strdup_local(description);
            runtime_tool->parameters_json = agnc_mcp_tools_normalize_parameters(schema_text);
            runtime_tool->server_index = server_index;

            free(schema_text);

            if (runtime_tool->mcp_tool_name == NULL || runtime_tool->description == NULL ||
                runtime_tool->parameters_json == NULL) {
                agnc_mcp_tool_catalog_free(catalog);
                free(tools);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }

            catalog_index++;
        }

        yyjson_doc_free(doc);
    }

    catalog->tools = tools;
    catalog->count = catalog_index;
    return AGNC_STATUS_OK;
}

const agnc_mcp_runtime_tool_t *agnc_mcp_tool_catalog_find(
    const agnc_mcp_tool_catalog_t *catalog,
    const char *exposed_name)
{
    size_t index;

    if (catalog == NULL || exposed_name == NULL) {
        return NULL;
    }

    for (index = 0; index < catalog->count; index++) {
        if (catalog->tools[index].exposed_name != NULL &&
            strcmp(catalog->tools[index].exposed_name, exposed_name) == 0) {
            return &catalog->tools[index];
        }
    }

    return NULL;
}
