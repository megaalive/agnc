/*
 * registry.c
 *
 * Pencarian gateway di registry hasil generate_integrations.py.
 */

#include "agnc/provider.h"

#include "agnc_integrations_gen.h"

#include <string.h>

const agnc_gateway_descriptor_t *agnc_registry_find_gateway(const char *id)
{
    size_t index;

    if (id == NULL || id[0] == '\0') {
        return NULL;
    }

    for (index = 0; index < AGNC_GATEWAY_COUNT; index++) {
        const agnc_gateway_descriptor_t *gateway = agnc_registry_gateway_at(index);
        if (gateway != NULL && strcmp(gateway->id, id) == 0) {
            return gateway;
        }
    }

    return NULL;
}

size_t agnc_registry_gateway_count(void)
{
    return AGNC_GATEWAY_COUNT;
}

const char *agnc_provider_resolve_api_model(const agnc_gateway_descriptor_t *gateway, const char *model)
{
    size_t index;

    if (gateway == NULL || model == NULL) {
        return model;
    }

    for (index = 0; index < gateway->model_count; index++) {
        const agnc_model_descriptor_t *entry = &gateway->models[index];
        if (entry->id != NULL && strcmp(entry->id, model) == 0) {
            return entry->api_name != NULL ? entry->api_name : model;
        }
    }

    return model;
}
