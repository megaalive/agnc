/*
 * cli.h
 *
 * Antarmuka publik modul CLI agnc.
 * Berisi struktur opsi hasil parsing argumen dan fungsi-fungsi
 * untuk menjalankan subcommand dasar (version, doctor, help, print).
 */

#ifndef AGNC_CLI_H
#define AGNC_CLI_H

#include "agnc/export.h"
#include "agnc/status.h"

/*
 * Hasil parsing argumen baris perintah.
 * Setiap field bernilai 1 jika flag/subcommand terkait aktif.
 */
typedef struct {
    int show_version; /* `agnc --version` */
    int show_doctor;  /* `agnc doctor` */
    int show_help;    /* `agnc --help` */
    int show_interactive; /* `agnc` tanpa argumen — REPL interaktif */
    int show_print;   /* `agnc --print "prompt"` */
    int show_models;  /* `agnc models [provider_id]` */
    int models_json;  /* `agnc models --json` */
    char *models_provider_filter;
    char *models_name_filter; /* `agnc models --filter nemotron` */
    int no_tools;     /* `agnc --no-tools` — chat tanpa tool schema */
    int auto_approve; /* `agnc --yes` — setujui shell tanpa prompt */
    char *print_prompt; /* Prompt untuk mode headless */
    int show_serve;   /* `agnc serve` */
    char *serve_listen; /* `--listen HOST:PORT` */
} agnc_cli_options_t;

/* Implementasi di src/cli/args.c — diekspor agar bisa dipanggil lintas TU. */
AGNC_API void agnc_cli_options_free_impl(agnc_cli_options_t *options);
AGNC_API agnc_status_t agnc_cli_parse_impl(int argc, char **argv, agnc_cli_options_t *options);
AGNC_API int agnc_cli_run_version_impl(void);
AGNC_API int agnc_cli_run_help_impl(void);

/* Wrapper inline: API stabil untuk main.c tanpa duplikasi simbol di args.c. */
static inline void agnc_cli_options_free(agnc_cli_options_t *options)
{
    agnc_cli_options_free_impl(options);
}

static inline agnc_status_t agnc_cli_parse(int argc, char **argv, agnc_cli_options_t *options)
{
    return agnc_cli_parse_impl(argc, argv, options);
}

static inline int agnc_cli_run_version(void)
{
    return agnc_cli_run_version_impl();
}

/* Jalankan subcommand doctor; return exit code proses. */
int agnc_cli_run_doctor(void);

static inline int agnc_cli_run_help(void)
{
    return agnc_cli_run_help_impl();
}

/* Jalankan mode headless --print; return exit code proses. */
int agnc_cli_run_print(const char *prompt, int no_tools, int auto_approve);

/* Discovery model untuk semua provider config; filter opsional. */
int agnc_cli_run_models(const char *provider_id_filter, const char *name_filter, int json_output);

/* Sama seperti run_models, tapi pakai provider/model aktif REPL untuk marker *.
 * Return 0 sukses, 1 error, 2 dibatalkan (cancel_flag). */
int agnc_cli_show_models(
    const char *provider_id_filter,
    const char *name_filter,
    const char *active_provider_id,
    const char *active_model,
    volatile int *cancel_flag);

/* Parse argumen teks REPL/CLI untuk /models. */
agnc_status_t agnc_cli_models_parse_query(
    const char *args,
    char **provider_out,
    char **name_filter_out);

/* Jalankan REPL interaktif (Fase 4). */
int agnc_cli_run_interactive(void);

/* Jalankan gRPC server headless (`agnc serve`). */
int agnc_cli_run_serve(const char *listen_address);

#endif /* AGNC_CLI_H */
