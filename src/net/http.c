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
} agnc_http_stream_state_t;

/*
 * Callback libcurl: setiap chunk respons (SSE atau JSON utuh) diteruskan ke parser.
 * Mengembalikan 0 menghentikan transfer jika parser melaporkan error.
 */
static size_t agnc_http_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    agnc_http_stream_state_t *state = (agnc_http_stream_state_t *)userdata;
    size_t total = size * nmemb;

    if (state->callback != NULL) {
        agnc_status_t status = state->callback(state->user_data, ptr, total);
        if (status != AGNC_STATUS_OK) {
            state->status = status;
            return 0;
        }
    }

    return total;
}

agnc_status_t agnc_http_post_stream(
    const char *url,
    const char *auth_header,
    const char *json_body,
    agnc_http_stream_cb callback,
    void *user_data,
    char **error_message)
{
    CURL *curl;
    CURLcode code;
    struct curl_slist *headers = NULL;
    agnc_http_stream_state_t state;
    long http_code = 0;
    char error_buffer[CURL_ERROR_SIZE];

    if (url == NULL || auth_header == NULL || json_body == NULL) {
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

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);
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
        return state.status;
    }

    if (code != CURLE_OK) {
        if (error_message != NULL) {
            *error_message = agnc_strdup_local(error_buffer);
        }
        return AGNC_STATUS_HTTP_ERROR;
    }

    /* 401 = key salah; 404 = model/tidak support tools; 429 = rate limit OpenRouter. */
    if (http_code < 200 || http_code >= 300) {
        if (error_message != NULL) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "HTTP status %ld", http_code);
            *error_message = agnc_strdup_local(buffer);
        }
        return AGNC_STATUS_HTTP_ERROR;
    }

    return AGNC_STATUS_OK;
}
