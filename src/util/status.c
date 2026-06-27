/*
 * status.c
 *
 * Konversi kode status internal agnc menjadi string untuk logging
 * dan output diagnostik (misalnya di perintah doctor).
 */

#include "agnc/status.h"

const char *agnc_status_to_string(agnc_status_t status)
{
    switch (status) {
    case AGNC_STATUS_OK:
        return "ok";
    case AGNC_STATUS_INVALID_ARGUMENT:
        return "invalid_argument";
    case AGNC_STATUS_IO_ERROR:
        return "io_error";
    case AGNC_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case AGNC_STATUS_JSON_ERROR:
        return "json_error";
    case AGNC_STATUS_HTTP_ERROR:
        return "http_error";
    case AGNC_STATUS_PROVIDER_ERROR:
        return "provider_error";
    case AGNC_STATUS_TOOL_DENIED:
        return "tool_denied";
    case AGNC_STATUS_TOOL_FAILED:
        return "tool_failed";
    default:
        return "unknown";
    }
}
