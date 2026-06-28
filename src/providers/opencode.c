/*
 * opencode.c
 *
 * Client HTTP native untuk opencode serve (session + message API).
 */

#include "agnc/opencode.h"

#include "agnc/net/http.h"
#include "agnc/provider.h"
#include "agnc/session.h"

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

static char *agnc_opencode_join_url(const char *base_url, const char *path)
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

static const char *agnc_opencode_base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *agnc_opencode_base64_encode(const unsigned char *data, size_t length)
{
    size_t out_len = 4 * ((length + 2) / 3);
    char *out;
    size_t index;
    size_t out_index = 0;

    out = (char *)malloc(out_len + 1);
    if (out == NULL) {
        return NULL;
    }

    for (index = 0; index < length; index += 3) {
        unsigned int octet_a = index < length ? data[index] : 0;
        unsigned int octet_b = index + 1 < length ? data[index + 1] : 0;
        unsigned int octet_c = index + 2 < length ? data[index + 2] : 0;
        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[out_index++] = agnc_opencode_base64_table[(triple >> 18) & 0x3F];
        out[out_index++] = agnc_opencode_base64_table[(triple >> 12) & 0x3F];
        out[out_index++] = index + 1 < length ? agnc_opencode_base64_table[(triple >> 6) & 0x3F] : '=';
        out[out_index++] = index + 2 < length ? agnc_opencode_base64_table[triple & 0x3F] : '=';
    }

    out[out_index] = '\0';
    return out;
}

static char *agnc_opencode_build_auth_header(const agnc_config_t *config)
{
    const char *password;
    const char *username;
    char credentials[512];
    char *encoded;
    char *header;
    size_t header_len;

    (void)config;

    password = getenv("OPENCODE_SERVER_PASSWORD");
    if (password == NULL || password[0] == '\0') {
        return NULL;
    }

    username = getenv("OPENCODE_SERVER_USERNAME");
    if (username == NULL || username[0] == '\0') {
        username = "opencode";
    }

    snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
    encoded = agnc_opencode_base64_encode((const unsigned char *)credentials, strlen(credentials));
    if (encoded == NULL) {
        return NULL;
    }

    header_len = strlen("Authorization: Basic ") + strlen(encoded) + 1;
    header = (char *)malloc(header_len);
    if (header == NULL) {
        free(encoded);
        return NULL;
    }

    snprintf(header, header_len, "Authorization: Basic %s", encoded);
    free(encoded);
    return header;
}

static const char *agnc_opencode_base(const agnc_config_t *config)
{
    if (config != NULL && config->base_url != NULL && config->base_url[0] != '\0') {
        return config->base_url;
    }
    return AGNC_OPENCODE_DEFAULT_BASE_URL;
}

static char *agnc_opencode_extract_error_message(const char *body)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *data;
    yyjson_val *message;
    const char *text;

    if (body == NULL || body[0] == '\0') {
        return NULL;
    }

    doc = yyjson_read(body, strlen(body), 0);
    if (doc == NULL) {
        return NULL;
    }

    root = yyjson_doc_get_root(doc);
    message = yyjson_obj_get(root, "message");
    if (message != NULL && yyjson_is_str(message)) {
        text = yyjson_get_str(message);
        if (text[0] != '\0') {
            char *copy = agnc_strdup_local(text);
            yyjson_doc_free(doc);
            return copy;
        }
    }

    data = yyjson_obj_get(root, "data");
    if (data != NULL && yyjson_is_obj(data)) {
        message = yyjson_obj_get(data, "message");
        if (message != NULL && yyjson_is_str(message)) {
            text = yyjson_get_str(message);
            if (text[0] != '\0') {
                char *copy = agnc_strdup_local(text);
                yyjson_doc_free(doc);
                return copy;
            }
        }
    }

    yyjson_doc_free(doc);
    return NULL;
}

agnc_status_t agnc_opencode_parse_model(
    const char *model,
    char **provider_id_out,
    char **model_id_out)
{
    const char *slash;
    char *provider_id;
    char *model_id;

    if (provider_id_out == NULL || model_id_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *provider_id_out = NULL;
    *model_id_out = NULL;

    if (model == NULL || model[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    slash = strchr(model, '/');
    if (slash == NULL) {
        provider_id = agnc_strdup_local("opencode");
        model_id = agnc_strdup_local(model);
    } else {
        size_t provider_len = (size_t)(slash - model);
        if (provider_len == 0 || slash[1] == '\0') {
            return AGNC_STATUS_INVALID_ARGUMENT;
        }
        provider_id = (char *)malloc(provider_len + 1);
        if (provider_id == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        memcpy(provider_id, model, provider_len);
        provider_id[provider_len] = '\0';
        model_id = agnc_strdup_local(slash + 1);
    }

    if (provider_id == NULL || model_id == NULL) {
        free(provider_id);
        free(model_id);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    *provider_id_out = provider_id;
    *model_id_out = model_id;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_opencode_probe(
    const char *base_url,
    char *detail,
    size_t detail_size)
{
    const char *base = base_url;
    char *url = NULL;
    char *response = NULL;
    char *error_message = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *healthy;
    yyjson_val *version;
    agnc_status_t status;

    if (base == NULL || base[0] == '\0') {
        base = AGNC_OPENCODE_DEFAULT_BASE_URL;
    }

    url = agnc_opencode_join_url(base, "/global/health");
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
    healthy = yyjson_obj_get(root, "healthy");
    version = yyjson_obj_get(root, "version");

    if (detail != NULL && detail_size > 0) {
        if (healthy != NULL && yyjson_is_bool(healthy) && yyjson_get_bool(healthy) &&
            version != NULL && yyjson_is_str(version)) {
            snprintf(detail, detail_size, "v%s at %s", yyjson_get_str(version), base);
        } else {
            snprintf(detail, detail_size, "reachable at %s", base);
        }
    }

    yyjson_doc_free(doc);
    free(error_message);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_opencode_list_models(
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
    yyjson_val *providers;
    size_t provider_index;
    size_t provider_count;
    char **ids = NULL;
    size_t count = 0;
    size_t capacity = 0;
    agnc_status_t status;

    if (config == NULL || model_ids == NULL || model_count == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *model_ids = NULL;
    *model_count = 0;

    url = agnc_opencode_join_url(agnc_opencode_base(config), "/config/providers");
    auth_header = agnc_opencode_build_auth_header(config);
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
    providers = yyjson_obj_get(root, "providers");
    if (providers == NULL || !yyjson_is_arr(providers)) {
        yyjson_doc_free(doc);
        free(error_message);
        return AGNC_STATUS_JSON_ERROR;
    }

    provider_count = yyjson_arr_size(providers);
    for (provider_index = 0; provider_index < provider_count; provider_index++) {
        yyjson_val *provider = yyjson_arr_get(providers, provider_index);
        yyjson_val *provider_id_val;
        yyjson_val *models_obj;
        const char *provider_id;

        if (provider == NULL || !yyjson_is_obj(provider)) {
            continue;
        }

        provider_id_val = yyjson_obj_get(provider, "id");
        models_obj = yyjson_obj_get(provider, "models");
        if (provider_id_val == NULL || !yyjson_is_str(provider_id_val) ||
            models_obj == NULL || !yyjson_is_obj(models_obj)) {
            continue;
        }

        provider_id = yyjson_get_str(provider_id_val);
        {
            yyjson_obj_iter model_iter;
            yyjson_obj_iter_init(models_obj, &model_iter);

            while (yyjson_obj_iter_has_next(&model_iter)) {
                yyjson_val *key_val = yyjson_obj_iter_next(&model_iter);
                const char *model_key;
                char combined[384];
                char *copy;

                if (key_val == NULL) {
                    continue;
                }

                model_key = yyjson_get_str(key_val);
                if (model_key == NULL || model_key[0] == '\0') {
                    continue;
                }

                snprintf(combined, sizeof(combined), "%s/%s", provider_id, model_key);
                if (count >= capacity) {
                    size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
                    char **resized = (char **)realloc(ids, new_capacity * sizeof(char *));
                    if (resized == NULL) {
                        yyjson_doc_free(doc);
                        agnc_provider_free_model_list(ids, count);
                        free(error_message);
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }
                    ids = resized;
                    capacity = new_capacity;
                }

                copy = agnc_strdup_local(combined);
                if (copy == NULL) {
                    yyjson_doc_free(doc);
                    agnc_provider_free_model_list(ids, count);
                    free(error_message);
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }

                ids[count++] = copy;
            }
        }
    }

    yyjson_doc_free(doc);
    free(error_message);

    *model_ids = ids;
    *model_count = count;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_opencode_create_session(
    const agnc_config_t *config,
    char **session_id_out,
    volatile int *cancel_flag)
{
    char *url = NULL;
    char *auth_header = NULL;
    char *response = NULL;
    char *error_message = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *id_val;
    agnc_status_t status;

    if (session_id_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *session_id_out = NULL;

    url = agnc_opencode_join_url(agnc_opencode_base(config), "/session");
    auth_header = agnc_opencode_build_auth_header(config);
    if (url == NULL) {
        free(auth_header);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_http_post(url, auth_header, "{}", &response, &error_message, cancel_flag, NULL);
    free(url);
    free(auth_header);

    if (status != AGNC_STATUS_OK) {
        if (error_message == NULL && response != NULL) {
            error_message = agnc_opencode_extract_error_message(response);
        }
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
    id_val = yyjson_obj_get(root, "id");
    if (id_val == NULL || !yyjson_is_str(id_val)) {
        yyjson_doc_free(doc);
        free(error_message);
        return AGNC_STATUS_JSON_ERROR;
    }

    *session_id_out = agnc_strdup_local(yyjson_get_str(id_val));
    yyjson_doc_free(doc);
    free(error_message);

    if (*session_id_out == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_opencode_ensure_session_id(
    const agnc_config_t *config,
    const char *session_sqlite_path,
    char **opencode_session_id_out,
    volatile int *cancel_flag)
{
    char *linked_id = NULL;
    agnc_status_t status;

    if (opencode_session_id_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *opencode_session_id_out = NULL;

    if (session_sqlite_path != NULL && session_sqlite_path[0] != '\0') {
        status = agnc_session_meta_get(session_sqlite_path, "opencode_session_id", &linked_id);
        if (status == AGNC_STATUS_OK && linked_id != NULL && linked_id[0] != '\0') {
            *opencode_session_id_out = linked_id;
            return AGNC_STATUS_OK;
        }
        free(linked_id);
        linked_id = NULL;
    }

    status = agnc_opencode_create_session(config, opencode_session_id_out, cancel_flag);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (session_sqlite_path != NULL && session_sqlite_path[0] != '\0') {
        (void)agnc_session_meta_set(session_sqlite_path, "opencode_session_id", *opencode_session_id_out);
    }

    return AGNC_STATUS_OK;
}

static int agnc_opencode_part_is_ignored(yyjson_val *part)
{
    yyjson_val *ignored_val;

    if (part == NULL) {
        return 0;
    }

    ignored_val = yyjson_obj_get(part, "ignored");
    return ignored_val != NULL && yyjson_is_bool(ignored_val) && yyjson_get_bool(ignored_val);
}

static char *agnc_opencode_append_chunk(char *combined, size_t *combined_len, size_t *combined_cap, const char *chunk)
{
    size_t chunk_len;

    if (chunk == NULL || chunk[0] == '\0') {
        return combined;
    }

    chunk_len = strlen(chunk);
    if (*combined_len + chunk_len + 2 > *combined_cap) {
        size_t new_cap = *combined_cap == 0 ? chunk_len + 64 : *combined_cap * 2;
        char *resized;

        while (new_cap < *combined_len + chunk_len + 2) {
            new_cap *= 2;
        }

        resized = (char *)realloc(combined, new_cap);
        if (resized == NULL) {
            free(combined);
            return NULL;
        }

        combined = resized;
        *combined_cap = new_cap;
    }

    if (*combined_len > 0) {
        combined[(*combined_len)++] = '\n';
    }
    memcpy(combined + *combined_len, chunk, chunk_len);
    *combined_len += chunk_len;
    combined[*combined_len] = '\0';
    return combined;
}

static char *agnc_opencode_collect_tool_output(yyjson_val *part)
{
    yyjson_val *state_val;
    yyjson_val *status_val;
    yyjson_val *output_val;
    yyjson_val *error_val;
    const char *status;
    const char *tool_name;
    char line[512];

    state_val = yyjson_obj_get(part, "state");
    if (state_val == NULL || !yyjson_is_obj(state_val)) {
        return NULL;
    }

    status_val = yyjson_obj_get(state_val, "status");
    if (status_val == NULL || !yyjson_is_str(status_val)) {
        return NULL;
    }

    status = yyjson_get_str(status_val);
    tool_name = "tool";
    {
        yyjson_val *tool_val = yyjson_obj_get(part, "tool");

        if (tool_val != NULL && yyjson_is_str(tool_val)) {
            tool_name = yyjson_get_str(tool_val);
        }
    }

    if (strcmp(status, "completed") == 0) {
        output_val = yyjson_obj_get(state_val, "output");
        if (output_val != NULL && yyjson_is_str(output_val) && yyjson_get_str(output_val)[0] != '\0') {
            return agnc_strdup_local(yyjson_get_str(output_val));
        }
        return NULL;
    }

    if (strcmp(status, "error") == 0) {
        error_val = yyjson_obj_get(state_val, "error");
        if (error_val != NULL && yyjson_is_str(error_val)) {
            snprintf(line, sizeof(line), "error (%s): %s", tool_name, yyjson_get_str(error_val));
        } else {
            snprintf(line, sizeof(line), "error (%s): tool failed", tool_name);
        }
        return agnc_strdup_local(line);
    }

    return NULL;
}

static char *agnc_opencode_diagnose_parts(yyjson_val *parts)
{
    size_t index;
    size_t count;
    char hint[512];
    size_t hint_len = 0;
    int text_ignored = 0;
    int text_empty = 0;
    int tool_pending = 0;
    int tool_other = 0;
    char tool_sample[96];

    tool_sample[0] = '\0';

    if (parts == NULL || !yyjson_is_arr(parts)) {
        return agnc_strdup_local("OpenCode: respons tanpa parts");
    }

    count = yyjson_arr_size(parts);
    if (count == 0) {
        return agnc_strdup_local("OpenCode: parts kosong");
    }

    for (index = 0; index < count; index++) {
        yyjson_val *part = yyjson_arr_get(parts, index);
        yyjson_val *type_val;
        const char *type;

        if (part == NULL || !yyjson_is_obj(part)) {
            continue;
        }

        type_val = yyjson_obj_get(part, "type");
        if (type_val == NULL || !yyjson_is_str(type_val)) {
            continue;
        }

        type = yyjson_get_str(type_val);
        if (strcmp(type, "text") == 0) {
            yyjson_val *text_val = yyjson_obj_get(part, "text");

            if (agnc_opencode_part_is_ignored(part)) {
                text_ignored++;
            } else if (text_val == NULL || !yyjson_is_str(text_val) || yyjson_get_str(text_val)[0] == '\0') {
                text_empty++;
            }
            continue;
        }

        if (strcmp(type, "tool") == 0) {
            yyjson_val *state_val = yyjson_obj_get(part, "state");
            yyjson_val *status_val;
            const char *status = "unknown";
            const char *tool_name = "tool";

            if (state_val != NULL && yyjson_is_obj(state_val)) {
                status_val = yyjson_obj_get(state_val, "status");
                if (status_val != NULL && yyjson_is_str(status_val)) {
                    status = yyjson_get_str(status_val);
                }
            }

            {
                yyjson_val *tool_val = yyjson_obj_get(part, "tool");

                if (tool_val != NULL && yyjson_is_str(tool_val)) {
                    tool_name = yyjson_get_str(tool_val);
                }
            }

            if (strcmp(status, "completed") != 0 && strcmp(status, "error") != 0) {
                tool_pending++;
                if (tool_sample[0] == '\0') {
                    snprintf(tool_sample, sizeof(tool_sample), "%s (%s)", tool_name, status);
                }
            } else {
                tool_other++;
            }
        }
    }

    hint[0] = '\0';
    if (tool_pending > 0) {
        hint_len += (size_t)snprintf(
            hint + hint_len,
            sizeof(hint) - hint_len,
            "tool belum selesai%s%s",
            tool_sample[0] != '\0' ? " (" : "",
            tool_sample[0] != '\0' ? tool_sample : "");
        if (tool_sample[0] != '\0') {
            hint_len += (size_t)snprintf(hint + hint_len, sizeof(hint) - hint_len, ")");
        }
    }
    if (text_ignored > 0) {
        if (hint_len > 0) {
            hint_len += (size_t)snprintf(hint + hint_len, sizeof(hint) - hint_len, "; ");
        }
        hint_len += (size_t)snprintf(
            hint + hint_len,
            sizeof(hint) - hint_len,
            "%d teks diabaikan OpenCode",
            text_ignored);
    }
    if (text_empty > 0 && text_ignored == 0 && tool_pending == 0) {
        if (hint_len > 0) {
            hint_len += (size_t)snprintf(hint + hint_len, sizeof(hint) - hint_len, "; ");
        }
        hint_len += (size_t)snprintf(
            hint + hint_len,
            sizeof(hint) - hint_len,
            "teks kosong dari model");
    }
    if (tool_other > 0 && tool_pending == 0 && hint_len == 0) {
        hint_len += (size_t)snprintf(
            hint + hint_len,
            sizeof(hint) - hint_len,
            "tool selesai tanpa output teks");
    }

    if (hint_len == 0) {
        snprintf(
            hint,
            sizeof(hint),
            "OpenCode mengembalikan %zu part tanpa teks tampil",
            count);
    } else {
        char prefixed[560];

        snprintf(prefixed, sizeof(prefixed), "OpenCode: %s", hint);
        return agnc_strdup_local(prefixed);
    }

    return agnc_strdup_local(hint);
}

static char *agnc_opencode_collect_text_parts(yyjson_val *parts)
{
    size_t index;
    size_t count;
    char *combined = NULL;
    size_t combined_len = 0;
    size_t combined_cap = 0;
    char *tool_fallback = NULL;
    size_t tool_fallback_len = 0;
    size_t tool_fallback_cap = 0;

    if (parts == NULL || !yyjson_is_arr(parts)) {
        return agnc_strdup_local("");
    }

    count = yyjson_arr_size(parts);
    for (index = 0; index < count; index++) {
        yyjson_val *part = yyjson_arr_get(parts, index);
        yyjson_val *type_val;
        yyjson_val *text_val;
        const char *type;
        const char *text;

        if (part == NULL || !yyjson_is_obj(part)) {
            continue;
        }

        type_val = yyjson_obj_get(part, "type");
        if (type_val == NULL || !yyjson_is_str(type_val)) {
            continue;
        }

        type = yyjson_get_str(type_val);
        if (strcmp(type, "text") == 0) {
            if (agnc_opencode_part_is_ignored(part)) {
                continue;
            }

            text_val = yyjson_obj_get(part, "text");
            if (text_val == NULL || !yyjson_is_str(text_val)) {
                continue;
            }

            text = yyjson_get_str(text_val);
            combined = agnc_opencode_append_chunk(combined, &combined_len, &combined_cap, text);
            if (combined == NULL && text[0] != '\0') {
                free(tool_fallback);
                return NULL;
            }
            continue;
        }

        if (strcmp(type, "tool") == 0) {
            char *tool_text = agnc_opencode_collect_tool_output(part);

            if (tool_text != NULL) {
                tool_fallback = agnc_opencode_append_chunk(
                    tool_fallback, &tool_fallback_len, &tool_fallback_cap, tool_text);
                free(tool_text);
                if (tool_fallback == NULL) {
                    free(combined);
                    return NULL;
                }
            }
        }
    }

    if (combined != NULL && combined[0] != '\0') {
        free(tool_fallback);
        return combined;
    }

    free(combined);
    if (tool_fallback != NULL && tool_fallback[0] != '\0') {
        return tool_fallback;
    }

    free(tool_fallback);
    return agnc_strdup_local("");
}

/*
 * Satu turn chat via POST /session/{id}/message (non-streaming).
 * Session OpenCode di-link ke SQLite agnc lewat meta opencode_session_id.
 */
agnc_status_t agnc_opencode_run_turn(
    const agnc_config_t *config,
    const char *session_sqlite_path,
    const char *system_prompt,
    const char *user_text,
    agnc_sse_parser_t *parser,
    char **error_message,
    volatile int *cancel_flag)
{
    char *provider_id = NULL;
    char *model_id = NULL;
    char *opencode_session_id = NULL;
    char *url = NULL;
    char *auth_header = NULL;
    char *request_json = NULL;
    char *response = NULL;
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root;
    yyjson_mut_val *parts;
    yyjson_mut_val *part;
    yyjson_mut_val *model_obj;
    char *assistant_text = NULL;
    agnc_status_t status;

    if (config == NULL || parser == NULL || user_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (cancel_flag != NULL && *cancel_flag) {
        return AGNC_STATUS_CANCELLED;
    }

    status = agnc_opencode_parse_model(config->model, &provider_id, &model_id);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_opencode_ensure_session_id(config, session_sqlite_path, &opencode_session_id, cancel_flag);
    if (status != AGNC_STATUS_OK) {
        free(provider_id);
        free(model_id);
        return status;
    }

    url = agnc_opencode_join_url(agnc_opencode_base(config), "/session/");
    if (url == NULL) {
        free(provider_id);
        free(model_id);
        free(opencode_session_id);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    {
        size_t url_len = strlen(url) + strlen(opencode_session_id) + strlen("/message") + 1;
        char *message_url = (char *)realloc(url, url_len);

        if (message_url == NULL) {
            free(url);
            free(provider_id);
            free(model_id);
            free(opencode_session_id);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        url = message_url;
        strcat(url, opencode_session_id);
        strcat(url, "/message");
    }

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        free(url);
        free(provider_id);
        free(model_id);
        free(opencode_session_id);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    parts = yyjson_mut_arr(doc);
    part = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, part, "type", "text");
    yyjson_mut_obj_add_str(doc, part, "text", user_text);
    yyjson_mut_arr_append(parts, part);
    yyjson_mut_obj_add_val(doc, root, "parts", parts);

    model_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, model_obj, "providerID", provider_id);
    yyjson_mut_obj_add_str(doc, model_obj, "modelID", model_id);
    yyjson_mut_obj_add_val(doc, root, "model", model_obj);

    if (system_prompt != NULL && system_prompt[0] != '\0') {
        yyjson_mut_obj_add_str(doc, root, "system", system_prompt);
    }

    request_json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    doc = NULL;

    if (request_json == NULL) {
        free(url);
        free(provider_id);
        free(model_id);
        free(opencode_session_id);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    auth_header = agnc_opencode_build_auth_header(config);
    status = agnc_http_post(url, auth_header, request_json, &response, error_message, cancel_flag, NULL);

    free(url);
    free(auth_header);
    free(request_json);
    free(provider_id);
    free(model_id);
    free(opencode_session_id);

    if (status != AGNC_STATUS_OK) {
        if (error_message != NULL && *error_message == NULL && response != NULL) {
            *error_message = agnc_opencode_extract_error_message(response);
        }
        free(response);
        return status;
    }

    {
        yyjson_doc *response_doc = yyjson_read(response, strlen(response), 0);
        yyjson_val *response_root;
        yyjson_val *response_parts;
        yyjson_val *info;
        yyjson_val *tokens;

        free(response);
        if (response_doc == NULL) {
            return AGNC_STATUS_JSON_ERROR;
        }

        response_root = yyjson_doc_get_root(response_doc);
        response_parts = yyjson_obj_get(response_root, "parts");
        assistant_text = agnc_opencode_collect_text_parts(response_parts);

        if (assistant_text != NULL && assistant_text[0] == '\0') {
            char *diagnosis = agnc_opencode_diagnose_parts(response_parts);

            if (diagnosis != NULL) {
                agnc_sse_parser_set_empty_hint(parser, diagnosis);
                if (config->verbose) {
                    fprintf(stderr, "agnc: opencode empty response: %s\n", diagnosis);
                }
                free(diagnosis);
            }
        }

        info = yyjson_obj_get(response_root, "info");
        if (info != NULL && yyjson_is_obj(info)) {
            tokens = yyjson_obj_get(info, "tokens");
            if (tokens != NULL && yyjson_is_obj(tokens)) {
                yyjson_val *input = yyjson_obj_get(tokens, "input");
                yyjson_val *output = yyjson_obj_get(tokens, "output");
                if (input != NULL && yyjson_is_num(input)) {
                    parser->prompt_tokens = (long)yyjson_get_num(input);
                }
                if (output != NULL && yyjson_is_num(output)) {
                    parser->completion_tokens = (long)yyjson_get_num(output);
                }
                if (parser->prompt_tokens >= 0 && parser->completion_tokens >= 0) {
                    parser->total_tokens = parser->prompt_tokens + parser->completion_tokens;
                }
            }
        }

        yyjson_doc_free(response_doc);
    }

    if (assistant_text == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    agnc_sse_parser_set_assistant_content(parser, assistant_text);
    free(assistant_text);
    return AGNC_STATUS_OK;
}

void agnc_opencode_clear_session_link(const char *session_sqlite_path)
{
    if (session_sqlite_path == NULL || session_sqlite_path[0] == '\0') {
        return;
    }

    (void)agnc_session_meta_delete(session_sqlite_path, "opencode_session_id");
}
