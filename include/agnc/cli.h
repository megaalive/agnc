/*
 * cli.h
 *
 * Antarmuka publik modul CLI agnc.
 * Berisi struktur opsi hasil parsing argumen dan fungsi-fungsi
 * untuk menjalankan subcommand dasar (version, doctor, help, print).
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
    int show_print;   /* `agnc --print "prompt"` */
    int no_tools;     /* `agnc --no-tools` — chat tanpa tool schema */
    char *print_prompt; /* Prompt untuk mode headless */
} agnc_cli_options_t;

void agnc_cli_options_free(agnc_cli_options_t *options);

/* Parse argv ke agnc_cli_options_t. */
agnc_status_t agnc_cli_parse(int argc, char **argv, agnc_cli_options_t *options);

/* Jalankan subcommand --version; return exit code proses. */
int agnc_cli_run_version(void);

/* Jalankan subcommand doctor; return exit code proses. */
int agnc_cli_run_doctor(void);

/* Tampilkan help singkat; return exit code proses. */
int agnc_cli_run_help(void);

/* Jalankan mode headless --print; return exit code proses. */
int agnc_cli_run_print(const char *prompt, int no_tools);

#endif
