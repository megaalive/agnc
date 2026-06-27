/*
 * http.h
 *
 * Wrapper tipis libcurl untuk POST streaming (SSE).
 */

#ifndef AGNC_NET_HTTP_H
#define AGNC_NET_HTTP_H

#include "agnc/status.h"

typedef struct agnc_http_stream_ctx agnc_http_stream_ctx_t;

typedef agnc_status_t (*agnc_http_stream_cb)(void *user_data, const char *chunk, size_t length);

agnc_status_t agnc_http_post_stream(
    const char *url,
    const char *auth_header,
    const char *json_body,
    agnc_http_stream_cb callback,
    void *user_data,
    char **error_message,
    volatile int *cancel_flag);

/* GET sinkron; mengumpulkan body utuh (mis. /models discovery). */
agnc_status_t agnc_http_get(
    const char *url,
    const char *auth_header,
    char **response_body,
    char **error_message,
    volatile int *cancel_flag);

/* POST sinkron; mengembalikan body respons utuh (non-SSE). */
agnc_status_t agnc_http_post(
    const char *url,
    const char *auth_header,
    const char *json_body,
    char **response_body,
    char **error_message,
    volatile int *cancel_flag);

#endif
