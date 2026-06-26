/*
 * main.c
 *
 * Titik masuk utama binary agnc.
 * Modul ini hanya mengarahkan alur eksekusi ke parser CLI, lalu
 * menjalankan subcommand yang diminta pengguna.
 */

#include "agnc/cli.h"
#include "agnc/version.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    agnc_cli_options_t options;
    agnc_status_t status;

    /* Parse argumen baris perintah ke struktur opsi yang terstruktur. */
    status = agnc_cli_parse(argc, argv, &options);
    if (status != AGNC_STATUS_OK) {
        /* Argumen tidak dikenal: tampilkan help agar pengguna tahu format yang benar. */
        agnc_cli_run_help();
        return 1;
    }

    if (options.show_version) {
        return agnc_cli_run_version();
    }

    if (options.show_doctor) {
        return agnc_cli_run_doctor();
    }

    /* Default fallback ke help (termasuk saat tidak ada argumen). */
    return agnc_cli_run_help();
}
