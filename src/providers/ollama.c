/*
 * ollama.c
 *
 * Health check Ollama lokal via endpoint OpenAI-compatible /models.
 */

#include "agnc/ollama.h"

#include "agnc/net/http.h"
#include "agnc/provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

agnc_status_t agnc_ollama_probe(
    const char *base_url_v1,
    size_t *model_count_out,
    char *detail,
    size_t detail_size)
{
    const agnc_gateway_descriptor_t *gateway;
    char *url = NULL;
    char *response = NULL;
    char *error_message = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *data;
    size_t count = 0;
    agnc_status_t status;
    const char *base = base_url_v1;

    if (model_count_out != NULL) {
        *model_count_out = 0;
    }

    if (base == NULL || base[0] == '\0') {
        base = AGNC_OLLAMA_DEFAULT_BASE_URL;
    }

    gateway = agnc_registry_find_gateway("ollama");
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    url = agnc_provider_build_models_url(gateway, base);
    if (url == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_http_get(url, NULL, &response, &error_message, NULL);
    free(url);

    if (status != AGNC_STATUS_OK) {
        free(response);
        free(error_message);
        if (detail != NULL && detail_size > 0) {
            snprintf(detail, detail_size, "not reachable (%s)", base);
        }
        return status;
    }

    doc = yyjson_read(response, strlen(response), 0);
    free(response);
    if (doc == NULL) {
        free(error_message);
        if (detail != NULL && detail_size > 0) {
            snprintf(detail, detail_size, "invalid JSON from %s", base);
        }
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    data = yyjson_obj_get(root, "data");
    if (data != NULL && yyjson_is_arr(data)) {
        count = yyjson_arr_size(data);
    }

    yyjson_doc_free(doc);
    free(error_message);

    if (model_count_out != NULL) {
        *model_count_out = count;
    }

    if (detail != NULL && detail_size > 0) {
        if (count > 0) {
            snprintf(detail, detail_size, "%zu model(s) at %s", count, base);
        } else {
            snprintf(detail, detail_size, "reachable, no models pulled at %s", base);
        }
    }

    return AGNC_STATUS_OK;
}
