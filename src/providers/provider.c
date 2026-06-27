/*
 * provider.c
 *
 * Utilitas URL/auth provider dan model discovery (OpenAI-compatible /models).
 */

#include "agnc/provider.h"
#include "agnc/opencode.h"
#include "agnc/config.h"

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

static agnc_status_t agnc_provider_list_catalog_models(
    const agnc_gateway_descriptor_t *gateway,
    char ***model_ids,
    size_t *model_count)
{
    size_t index;
    char **ids;

    if (gateway == NULL || model_ids == NULL || model_count == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *model_ids = NULL;
    *model_count = 0;

    if (gateway->model_count == 0 || gateway->models == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    ids = (char **)calloc(gateway->model_count, sizeof(char *));
    if (ids == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < gateway->model_count; index++) {
        const char *name = gateway->models[index].api_name;

        if (name == NULL || name[0] == '\0') {
            name = gateway->models[index].id;
        }
        if (name == NULL || name[0] == '\0') {
            agnc_provider_free_model_list(ids, index);
            return AGNC_STATUS_INVALID_ARGUMENT;
        }

        ids[index] = agnc_strdup_local(name);
        if (ids[index] == NULL) {
            agnc_provider_free_model_list(ids, index);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    *model_ids = ids;
    *model_count = gateway->model_count;
    return AGNC_STATUS_OK;
}

static int agnc_provider_supports_dynamic_models(const agnc_gateway_descriptor_t *gateway)
{
    if (gateway == NULL) {
        return 0;
    }

    return gateway->transport_kind == AGNC_TRANSPORT_OPENAI_COMPATIBLE ||
           gateway->transport_kind == AGNC_TRANSPORT_OPENCODE_NATIVE;
}

static agnc_status_t agnc_provider_list_models_openai_compat(
    const agnc_gateway_descriptor_t *gateway,
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count,
    volatile int *cancel_flag);

agnc_status_t agnc_provider_list_models(
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count,
    volatile int *cancel_flag)
{
    const agnc_gateway_descriptor_t *gateway;
    agnc_status_t dynamic_status;

    if (config == NULL || model_ids == NULL || model_count == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (cancel_flag != NULL && *cancel_flag) {
        return AGNC_STATUS_CANCELLED;
    }

    *model_ids = NULL;
    *model_count = 0;

    gateway = agnc_registry_find_gateway(config->gateway_id);
    if (gateway == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (agnc_provider_supports_dynamic_models(gateway)) {
        if (gateway->transport_kind == AGNC_TRANSPORT_OPENCODE_NATIVE) {
            dynamic_status = agnc_opencode_list_models(config, model_ids, model_count, cancel_flag);
        } else {
            dynamic_status =
                agnc_provider_list_models_openai_compat(gateway, config, model_ids, model_count, cancel_flag);
        }

        if (dynamic_status == AGNC_STATUS_CANCELLED) {
            return AGNC_STATUS_CANCELLED;
        }

        if (dynamic_status == AGNC_STATUS_OK && *model_count > 0) {
            return AGNC_STATUS_OK;
        }

        if (gateway->model_count > 0) {
            agnc_provider_free_model_list(*model_ids, *model_count);
            *model_ids = NULL;
            *model_count = 0;
            return agnc_provider_list_catalog_models(gateway, model_ids, model_count);
        }

        return dynamic_status;
    }

    if (gateway->model_count > 0) {
        return agnc_provider_list_catalog_models(gateway, model_ids, model_count);
    }

    return AGNC_STATUS_INVALID_ARGUMENT;
}

static agnc_status_t agnc_provider_list_models_openai_compat(
    const agnc_gateway_descriptor_t *gateway,
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count,
    volatile int *cancel_flag)
{
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

    if (gateway == NULL || config == NULL || model_ids == NULL || model_count == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *model_ids = NULL;
    *model_count = 0;

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

    status = agnc_http_get(url, auth_header, &response, &error_message, cancel_flag);
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

void agnc_provider_models_snapshots_free(agnc_provider_models_snapshot_t *snapshots, size_t count)
{
    size_t index;

    if (snapshots == NULL) {
        return;
    }

    for (index = 0; index < count; index++) {
        free(snapshots[index].provider_id);
        free(snapshots[index].gateway_id);
        free(snapshots[index].base_url);
        free(snapshots[index].default_model);
        free(snapshots[index].error_message);
        agnc_provider_free_model_list(snapshots[index].model_ids, snapshots[index].model_count);
    }
    free(snapshots);
}

/*
 * Iterasi entri providers{} di config; tiap provider dapat snapshot model + status.
 * cancel_flag opsional — dipakai REPL saat /models (Ctrl+C antar provider/HTTP).
 */
agnc_status_t agnc_provider_discover_configured(
    const char *config_path,
    const char *provider_id_filter,
    agnc_provider_models_snapshot_t **snapshots_out,
    size_t *snapshot_count_out,
    volatile int *cancel_flag)
{
    char **provider_ids = NULL;
    size_t provider_count = 0;
    agnc_provider_models_snapshot_t *snapshots = NULL;
    size_t snapshot_count = 0;
    size_t index;
    agnc_status_t status;

    if (snapshots_out == NULL || snapshot_count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *snapshots_out = NULL;
    *snapshot_count_out = 0;

    if (provider_id_filter != NULL && provider_id_filter[0] != '\0') {
        provider_ids = (char **)calloc(1, sizeof(char *));
        if (provider_ids == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        provider_ids[0] = agnc_strdup_local(provider_id_filter);
        if (provider_ids[0] == NULL) {
            free(provider_ids);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        provider_count = 1;
    } else {
        status = agnc_config_list_provider_ids(config_path, &provider_ids, &provider_count);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    if (provider_count == 0) {
        agnc_config_free_provider_id_list(provider_ids, provider_count);
        return AGNC_STATUS_OK;
    }

    snapshots = (agnc_provider_models_snapshot_t *)calloc(provider_count, sizeof(*snapshots));
    if (snapshots == NULL) {
        agnc_config_free_provider_id_list(provider_ids, provider_count);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < provider_count; index++) {
        agnc_config_t config;
        agnc_provider_models_snapshot_t *snapshot = &snapshots[snapshot_count];

        if (cancel_flag != NULL && *cancel_flag) {
            status = AGNC_STATUS_CANCELLED;
            goto cleanup;
        }

        agnc_config_init(&config);
        snapshot->provider_id = agnc_strdup_local(provider_ids[index]);
        if (snapshot->provider_id == NULL) {
            agnc_config_free(&config);
            status = AGNC_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }

        status = agnc_config_load_provider_entry(config_path, provider_ids[index], &config);
        snapshot->status = status;
        if (status != AGNC_STATUS_OK) {
            snapshot->error_message = agnc_strdup_local(agnc_status_to_string(status));
            if (snapshot->error_message == NULL) {
                agnc_config_free(&config);
                status = AGNC_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            agnc_config_free(&config);
            snapshot_count++;
            continue;
        }

        snapshot->gateway_id = agnc_strdup_local(config.gateway_id);
        snapshot->base_url = agnc_strdup_local(config.base_url);
        snapshot->default_model = agnc_strdup_local(config.model);
        if (snapshot->gateway_id == NULL || snapshot->base_url == NULL || snapshot->default_model == NULL) {
            agnc_config_free(&config);
            status = AGNC_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }

        snapshot->status = agnc_provider_list_models(&config, &snapshot->model_ids, &snapshot->model_count, cancel_flag);
        if (snapshot->status == AGNC_STATUS_CANCELLED) {
            agnc_config_free(&config);
            status = AGNC_STATUS_CANCELLED;
            goto cleanup;
        }
        if (snapshot->status != AGNC_STATUS_OK) {
            snapshot->error_message = agnc_strdup_local(agnc_status_to_string(snapshot->status));
            if (snapshot->error_message == NULL) {
                agnc_config_free(&config);
                status = AGNC_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
        }

        agnc_config_free(&config);
        snapshot_count++;
    }

    agnc_config_free_provider_id_list(provider_ids, provider_count);
    *snapshots_out = snapshots;
    *snapshot_count_out = snapshot_count;
    return AGNC_STATUS_OK;

cleanup:
    agnc_config_free_provider_id_list(provider_ids, provider_count);
    agnc_provider_models_snapshots_free(snapshots, snapshot_count);
    return status;
}
