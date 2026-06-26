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
    int exit_code = 0;

    status = agnc_cli_parse(argc, argv, &options);
    if (status != AGNC_STATUS_OK) {
        agnc_cli_run_help();
        agnc_cli_options_free(&options);
        return 1;
    }

    if (options.show_version) {
        exit_code = agnc_cli_run_version();
    } else if (options.show_doctor) {
        exit_code = agnc_cli_run_doctor();
    } else if (options.show_print) {
        exit_code = agnc_cli_run_print(options.print_prompt);
    } else {
        exit_code = agnc_cli_run_help();
    }

    agnc_cli_options_free(&options);
    return exit_code;
}
