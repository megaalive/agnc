/*
 * provider.c
 *
 * Utilitas URL/auth provider dan model discovery (OpenAI-compatible /models).
 */

#include "agnc/provider.h"

#include "agnc/net/http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static size_t agnc_trim_trailing_slash_length(const char *text)
{
    size_t length;

    if (text == NULL) {
        return 0;
    }

    length = strlen(text);
    while (length > 0 && text[length - 1] == '/') {
        length--;
    }
    return length;
}

static char *agnc_provider_join_url(const char *base_url, const char *path)
{
    size_t base_len;
    size_t path_len;
    size_t needs_slash;
    char *url;

    if (base_url == NULL || path == NULL) {
        return NULL;
    }

    base_len = agnc_trim_trailing_slash_length(base_url);
    path_len = strlen(path);
    needs_slash = (path[0] != '/') ? 1 : 0;

    url = (char *)malloc(base_len + path_len + needs_slash + 1);
    if (url == NULL) {
        return NULL;
    }

    memcpy(url, base_url, base_len);
    if (needs_slash) {
        url[base_len] = '/';
        memcpy(url + base_len + 1, path, path_len + 1);
    } else {
        memcpy(url + base_len, path, path_len + 1);
    }

    return url;
}

char *agnc_provider_build_chat_url(const agnc_gateway_descriptor_t *gateway, const char *base_url)
{
    const char *path;

    if (gateway == NULL || base_url == NULL) {
        return NULL;
    }

    path = gateway->endpoint_path != NULL ? gateway->endpoint_path : "/chat/completions";
    return agnc_provider_join_url(base_url, path);
}

char *agnc_provider_build_auth_header(const agnc_gateway_descriptor_t *gateway, const char *api_key)
{
    size_t length;
    char *header;
    const char *scheme;
    const char *name;

    if (gateway == NULL || api_key == NULL || api_key[0] == '\0') {
        return NULL;
    }

    name = gateway->auth_header_name != NULL ? gateway->auth_header_name : "Authorization";
    scheme = gateway->auth_header_scheme != NULL ? gateway->auth_header_scheme : "bearer";

    if (strcmp(scheme, "bearer") == 0) {
        length = strlen(name) + strlen(api_key) + 16;
        header = (char *)malloc(length);
        if (header == NULL) {
            return NULL;
        }
        snprintf(header, length, "%s: Bearer %s", name, api_key);
        return header;
    }

    length = strlen(name) + strlen(api_key) + 4;
    header = (char *)malloc(length);
    if (header == NULL) {
        return NULL;
    }
    snprintf(header, length, "%s: %s", name, api_key);
    return header;
}

char *agnc_provider_build_models_url(const agnc_gateway_descriptor_t *gateway, const char *base_url)
{
    const char *path;

    if (gateway == NULL || base_url == NULL) {
        return NULL;
    }

    path = gateway->models_endpoint_path != NULL ? gateway->models_endpoint_path : "/models";
    return agnc_provider_join_url(base_url, path);
}

void agnc_provider_free_model_list(char **model_ids, size_t model_count)
{
    size_t index;

    if (model_ids == NULL) {
        return;
    }

    for (index = 0; index < model_count; index++) {
        free(model_ids[index]);
    }
    free(model_ids);
}

agnc_status_t agnc_provider_list_models(
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count)
{
    const agnc_gateway_descriptor_t *gateway;
    char *url = NULL;
    char *auth_header = NULL;
    char *response = NULL;
    char *error_message = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *data;
    size_t index;
    size_t count;
    char **ids = NULL;
    agnc_status_t status;

    if (config == NULL || model_ids == NULL || model_count == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *model_ids = NULL;
    *model_count = 0;

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (gateway->transport_kind != AGNC_TRANSPORT_OPENAI_COMPATIBLE) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (config->base_url == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    url = agnc_provider_build_models_url(gateway, config->base_url);
    if (config->api_key != NULL && config->api_key[0] != '\0') {
        auth_header = agnc_provider_build_auth_header(gateway, config->api_key);
    }
    if (url == NULL) {
        free(auth_header);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_http_get(url, auth_header, &response, &error_message);
    free(url);
    free(auth_header);

    if (status != AGNC_STATUS_OK) {
        free(response);
        free(error_message);
        return status;
    }

    doc = yyjson_read(response, strlen(response), 0);
    free(response);
    if (doc == NULL) {
        free(error_message);
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    data = yyjson_obj_get(root, "data");
    if (data == NULL || !yyjson_is_arr(data)) {
        yyjson_doc_free(doc);
        free(error_message);
        return AGNC_STATUS_JSON_ERROR;
    }

    count = yyjson_arr_size(data);
    if (count == 0) {
        yyjson_doc_free(doc);
        free(error_message);
        return AGNC_STATUS_OK;
    }

    ids = (char **)calloc(count, sizeof(char *));
    if (ids == NULL) {
        yyjson_doc_free(doc);
        free(error_message);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < count; index++) {
        yyjson_val *item = yyjson_arr_get(data, index);
        yyjson_val *id_val = item != NULL ? yyjson_obj_get(item, "id") : NULL;
        if (id_val == NULL || !yyjson_is_str(id_val)) {
            agnc_provider_free_model_list(ids, index);
            yyjson_doc_free(doc);
            free(error_message);
            return AGNC_STATUS_JSON_ERROR;
        }
        ids[index] = agnc_strdup_local(yyjson_get_str(id_val));
        if (ids[index] == NULL) {
            agnc_provider_free_model_list(ids, index);
            yyjson_doc_free(doc);
            free(error_message);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    yyjson_doc_free(doc);
    free(error_message);
    *model_ids = ids;
    *model_count = count;
    return AGNC_STATUS_OK;
}
