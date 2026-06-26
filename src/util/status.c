/*
 * status.c
 *
 * Konversi kode status internal agnc menjadi string untuk logging
 * dan output diagnostik (misalnya di perintah doctor).
 */

#include "agnc/status.h"

/*
 * Mengubah enum agnc_status_t menjadi label teks pendek.
 * Label sengaja lowercase agar konsisten di seluruh output CLI.
 */
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
    default:
        /* Fallback untuk enum yang belum dipetakan ke string. */
        return "unknown";
    }
}
