/* File ini dihasilkan oleh scripts/generate_integrations.py — jangan edit manual. */
#ifndef AGNC_INTEGRATIONS_GEN_H
#define AGNC_INTEGRATIONS_GEN_H

#include "agnc/provider.h"

/* Registry gateway terurut; dipakai agnc_registry_find_gateway(). */
#define AGNC_GATEWAY_COUNT 5

const agnc_gateway_descriptor_t *agnc_registry_gateway_at(size_t index);

#endif /* AGNC_INTEGRATIONS_GEN_H */
