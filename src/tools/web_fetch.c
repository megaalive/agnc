/*
 * web_fetch.c
 *
 * Tool web_fetch: HTTP GET URL dan kembalikan teks (truncated).
 */

#include "agnc/tool.h"
#include "agnc/net/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_WEB_FETCH_MAX (64 * 1024)

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

const char *agnc_tool_web_fetch_url_preview(const char *arguments_json)
{
    static char preview[256];
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *url_value;

    preview[0] = '\0';
    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        snprintf(preview, sizeof(preview), "%.200s", arguments_json != NULL ? arguments_json : "");
        return preview;
    }

    root = yyjson_doc_get_root(doc);
    url_value = yyjson_obj_get(root, "url");
    if (url_value != NULL && yyjson_is_str(url_value)) {
        snprintf(preview, sizeof(preview), "%s", yyjson_get_str(url_value));
    }

    yyjson_doc_free(doc);
    return preview;
}

agnc_status_t agnc_tool_web_fetch_execute(const char *arguments_json, char **result_text)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *url_value;
    const char *url;
    char *body = NULL;
    char *error_message = NULL;
    agnc_status_t status;
    size_t length;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;
    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        *result_text = agnc_strdup_local("error: invalid JSON arguments");
        return AGNC_STATUS_TOOL_FAILED;
    }

    root = yyjson_doc_get_root(doc);
    url_value = yyjson_obj_get(root, "url");
    if (url_value == NULL || !yyjson_is_str(url_value)) {
        yyjson_doc_free(doc);
        *result_text = agnc_strdup_local("error: missing url argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    url = yyjson_get_str(url_value);
    if (url[0] == '\0' || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
        yyjson_doc_free(doc);
        *result_text = agnc_strdup_local("error: url must start with http:// or https://");
        return AGNC_STATUS_TOOL_FAILED;
    }

    yyjson_doc_free(doc);

    status = agnc_http_get(url, NULL, &body, &error_message);
    if (status != AGNC_STATUS_OK) {
        if (error_message != NULL) {
            *result_text = error_message;
        } else {
            *result_text = agnc_strdup_local("error: HTTP GET failed");
        }
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (body == NULL) {
        *result_text = agnc_strdup_local("(empty response)");
        return AGNC_STATUS_OK;
    }

    length = strlen(body);
    if (length > AGNC_WEB_FETCH_MAX) {
        body[AGNC_WEB_FETCH_MAX] = '\0';
        *result_text = (char *)realloc(body, AGNC_WEB_FETCH_MAX + 32);
        if (*result_text == NULL) {
            *result_text = body;
        } else {
            strcat(*result_text, "\n...(output truncated)");
        }
    } else {
        *result_text = body;
    }

    return AGNC_STATUS_OK;
}
