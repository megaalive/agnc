/*
 * cli.h
 *
 * Antarmuka publik modul CLI agnc.
 * Berisi struktur opsi hasil parsing argumen dan fungsi-fungsi
 * untuk menjalankan subcommand dasar (version, doctor, help).
 */

#ifndef AGNC_CLI_H
#define AGNC_CLI_H

#include "agnc/status.h"

/*
 * Hasil parsing argumen baris perintah.
 * Setiap field bernilai 1 jika flag/subcommand terkait aktif.
 */
typedef struct {
    int show_version; /* `agnc --version` */
    int show_doctor;  /* `agnc doctor` */
    int show_help;    /* `agnc --help` atau default tanpa argumen */
} agnc_cli_options_t;

/* Parse argv ke agnc_cli_options_t. */
agnc_status_t agnc_cli_parse(int argc, char **argv, agnc_cli_options_t *options);

/* Jalankan subcommand --version; return exit code proses. */
int agnc_cli_run_version(void);

/* Jalankan subcommand doctor; return exit code proses. */
int agnc_cli_run_doctor(void);

/* Tampilkan help singkat; return exit code proses. */
int agnc_cli_run_help(void);

#endif
