/*
 * http.c
 *
 * Implementasi POST streaming via libcurl untuk endpoint SSE OpenAI-compatible.
 */

#include "agnc/net/http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_HTTP_BODY_INITIAL 4096

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

typedef struct {
    agnc_http_stream_cb callback;
    void *user_data;
    agnc_status_t status;
    char *response_body;
    size_t response_length;
    size_t response_capacity;
    volatile int *cancel_flag;
} agnc_http_stream_state_t;

static agnc_status_t agnc_http_append_body(agnc_http_stream_state_t *state, const char *data, size_t length)
{
    if (state->response_length + length + 1 > state->response_capacity) {
        size_t new_capacity = state->response_capacity == 0 ? AGNC_HTTP_BODY_INITIAL : state->response_capacity * 2;
        char *new_buffer;

        while (new_capacity < state->response_length + length + 1) {
            new_capacity *= 2;
        }

        new_buffer = (char *)realloc(state->response_body, new_capacity);
        if (new_buffer == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        state->response_body = new_buffer;
        state->response_capacity = new_capacity;
    }

    memcpy(state->response_body + state->response_length, data, length);
    state->response_length += length;
    state->response_body[state->response_length] = '\0';
    return AGNC_STATUS_OK;
}

/*
 * Callback libcurl: setiap chunk respons (SSE atau JSON utuh) diteruskan ke parser.
 * Body juga diakumulasi untuk pesan error HTTP yang informatif.
 */
static size_t agnc_http_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    agnc_http_stream_state_t *state = (agnc_http_stream_state_t *)userdata;
    size_t total = size * nmemb;

    if (state->cancel_flag != NULL && *state->cancel_flag) {
        state->status = AGNC_STATUS_CANCELLED;
        return 0;
    }

    if (agnc_http_append_body(state, ptr, total) != AGNC_STATUS_OK) {
        state->status = AGNC_STATUS_OUT_OF_MEMORY;
        return 0;
    }

    if (state->callback != NULL) {
        agnc_status_t status = state->callback(state->user_data, ptr, total);
        if (status != AGNC_STATUS_OK) {
            state->status = status;
            return 0;
        }
    }

    return total;
}

static char *agnc_http_extract_provider_message(const char *body)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *error_obj;
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
    error_obj = yyjson_obj_get(root, "error");
    if (error_obj != NULL && yyjson_is_obj(error_obj)) {
        message = yyjson_obj_get(error_obj, "message");
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

static char *agnc_http_format_status_error(long http_code, const char *body)
{
    char *provider_message;
    char *result;
    const char *hint = NULL;
    size_t length;

    provider_message = agnc_http_extract_provider_message(body);

    if (http_code == 401) {
        hint = "Periksa API key (AGNC_API_KEY atau .keys/openrouter.txt).";
    } else if (http_code == 404) {
        hint = "Model mungkin tidak tersedia atau tidak mendukung tool use; coba --no-tools atau ganti model.";
    } else if (http_code == 429) {
        hint = "Rate limit OpenRouter; tunggu sebentar, ganti model, atau tambah kredit.";
    }

    /* Gabungkan pesan provider JSON dengan petunjuk status HTTP. */
    if (provider_message != NULL && hint != NULL) {
        length = strlen(provider_message) + strlen(hint) + 32;
        result = (char *)malloc(length);
        if (result != NULL) {
            snprintf(result, length, "HTTP %ld: %s (%s)", http_code, provider_message, hint);
        }
        free(provider_message);
        return result;
    }

    if (provider_message != NULL) {
        length = strlen(provider_message) + 24;
        result = (char *)malloc(length);
        if (result != NULL) {
            snprintf(result, length, "HTTP %ld: %s", http_code, provider_message);
        }
        free(provider_message);
        return result;
    }

    if (hint != NULL) {
        length = strlen(hint) + 24;
        result = (char *)malloc(length);
        if (result != NULL) {
            snprintf(result, length, "HTTP %ld (%s)", http_code, hint);
        }
        return result;
    }

    if (body != NULL && body[0] != '\0') {
        length = strlen(body) + 24;
        if (length > 512) {
            length = 512;
        }
        result = (char *)malloc(length);
        if (result != NULL) {
            snprintf(result, length, "HTTP %ld: %.450s", http_code, body);
        }
        return result;
    }

    result = (char *)malloc(32);
    if (result != NULL) {
        snprintf(result, 32, "HTTP status %ld", http_code);
    }
    return result;
}

agnc_status_t agnc_http_post_stream(
    const char *url,
    const char *auth_header,
    const char *json_body,
    agnc_http_stream_cb callback,
    void *user_data,
    char **error_message,
    volatile int *cancel_flag)
{
    CURL *curl;
    CURLcode code;
    struct curl_slist *headers = NULL;
    agnc_http_stream_state_t state;
    long http_code = 0;
    char error_buffer[CURL_ERROR_SIZE];
    agnc_status_t result_status = AGNC_STATUS_OK;

    if (url == NULL || json_body == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    memset(&state, 0, sizeof(state));
    state.callback = callback;
    state.user_data = user_data;
    state.status = AGNC_STATUS_OK;
    state.cancel_flag = cancel_flag;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (auth_header != NULL && auth_header[0] != '\0') {
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, agnc_http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (state.status != AGNC_STATUS_OK) {
        free(state.response_body);
        return state.status;
    }

    if (code != CURLE_OK) {
        if (error_message != NULL) {
            *error_message = agnc_strdup_local(error_buffer);
        }
        free(state.response_body);
        return AGNC_STATUS_HTTP_ERROR;
    }

    if (http_code < 200 || http_code >= 300) {
        if (error_message != NULL) {
            *error_message = agnc_http_format_status_error(http_code, state.response_body);
        }
        free(state.response_body);
        return AGNC_STATUS_HTTP_ERROR;
    }

    free(state.response_body);
    return result_status;
}

typedef struct {
    char *response_body;
    size_t response_length;
    size_t response_capacity;
} agnc_http_get_state_t;

static size_t agnc_http_get_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    agnc_http_get_state_t *state = (agnc_http_get_state_t *)userdata;
    size_t total = size * nmemb;
    agnc_http_stream_state_t stream_state;

    memset(&stream_state, 0, sizeof(stream_state));
    stream_state.response_body = state->response_body;
    stream_state.response_length = state->response_length;
    stream_state.response_capacity = state->response_capacity;

    if (agnc_http_append_body(&stream_state, ptr, total) != AGNC_STATUS_OK) {
        return 0;
    }

    state->response_body = stream_state.response_body;
    state->response_length = stream_state.response_length;
    state->response_capacity = stream_state.response_capacity;
    return total;
}

agnc_status_t agnc_http_get(
    const char *url,
    const char *auth_header,
    char **response_body,
    char **error_message)
{
    CURL *curl;
    CURLcode code;
    struct curl_slist *headers = NULL;
    agnc_http_get_state_t state;
    long http_code = 0;
    char error_buffer[CURL_ERROR_SIZE];

    if (url == NULL || response_body == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *response_body = NULL;
    if (error_message != NULL) {
        *error_message = NULL;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    memset(&state, 0, sizeof(state));

    if (auth_header != NULL && auth_header[0] != '\0') {
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, agnc_http_get_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        if (error_message != NULL) {
            *error_message = agnc_strdup_local(error_buffer);
        }
        free(state.response_body);
        return AGNC_STATUS_HTTP_ERROR;
    }

    if (http_code < 200 || http_code >= 300) {
        if (error_message != NULL) {
            *error_message = agnc_http_format_status_error(http_code, state.response_body);
        }
        free(state.response_body);
        return AGNC_STATUS_HTTP_ERROR;
    }

    *response_body = state.response_body;
    return AGNC_STATUS_OK;
}
