/*
 * status.h
 *
 * Kode status standar untuk seluruh modul agnc.
 * Pola ini dipakai agar error handling konsisten di seluruh codebase:
 * fungsi mengembalikan agnc_status_t, detail opsional lewat parameter output.
 */

#ifndef AGNC_STATUS_H
#define AGNC_STATUS_H

typedef enum {
    AGNC_STATUS_OK = 0,              /* Operasi berhasil */
    AGNC_STATUS_INVALID_ARGUMENT,      /* Parameter input tidak valid */
    AGNC_STATUS_IO_ERROR,            /* Gagal baca/tulis file atau env */
    AGNC_STATUS_OUT_OF_MEMORY        /* Alokasi heap gagal */
} agnc_status_t;

/* Konversi kode status ke string pendek untuk logging/doctor. */
const char *agnc_status_to_string(agnc_status_t status);

#endif
