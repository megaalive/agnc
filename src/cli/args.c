/*
 * args.c
 *
 * Implementasi parser argumen CLI dan perintah dasar (--version, --help).
 * Fase 0 sengaja memakai parser manual; nanti bisa diganti argtable3
 * saat jumlah flag bertambah (--print, --model, dll.).
 */

#include "agnc/cli.h"
#include "agnc/version.h"

#include <stdio.h>
#include <string.h>

/*
 * Mengubah argv[] menjadi struktur agnc_cli_options_t.
 * Return AGNC_STATUS_INVALID_ARGUMENT jika ada token yang tidak dikenal.
 */
agnc_status_t agnc_cli_parse(int argc, char **argv, agnc_cli_options_t *options)
{
    int index;

    if (options == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    /* Reset semua flag agar tidak ada sisa state dari pemanggil sebelumnya. */
    memset(options, 0, sizeof(*options));

    /* Lewati argv[0] karena itu nama program, bukan argumen pengguna. */
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--version") == 0 || strcmp(argv[index], "-V") == 0) {
            options->show_version = 1;
            continue;
        }

        if (strcmp(argv[index], "doctor") == 0 || strcmp(argv[index], "--doctor") == 0) {
            options->show_doctor = 1;
            continue;
        }

        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            options->show_help = 1;
            continue;
        }

        fprintf(stderr, "agnc: unknown argument: %s\n", argv[index]);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!options->show_version && !options->show_doctor && !options->show_help) {
        /*
         * Tanpa subcommand eksplisit, tampilkan help.
         * Ini mencegah CLI terasa "diam" saat pengguna menjalankan `agnc` saja.
         */
        options->show_help = 1;
    }

    return AGNC_STATUS_OK;
}

/* Mencetak versi agnc ke stdout; dipakai oleh `agnc --version`. */
int agnc_cli_run_version(void)
{
    printf("agnc %s\n", AGNC_VERSION_STRING);
    return 0;
}

/* Mencetak ringkasan perintah yang tersedia di Fase 0. */
int agnc_cli_run_help(void)
{
    printf("agnc - OpenClaude-compatible coding agent CLI (C)\n\n");
    printf("Usage:\n");
    printf("  agnc --version          Show version\n");
    printf("  agnc doctor             Check environment and dependencies\n");
    printf("  agnc --help             Show this help\n");
    return 0;
}
