/*
 * args.c
 *
 * Parser argumen CLI dan subcommand dasar (--version, --help, --print).
 * Flag --no-tools dan --yes bisa muncul sebelum atau sesudah --print.
 */

#include "agnc/cli.h"
#include "agnc/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_cli_options_free(agnc_cli_options_t *options)
{
    if (options == NULL) {
        return;
    }

    free(options->print_prompt);
    options->print_prompt = NULL;
}

agnc_status_t agnc_cli_parse(int argc, char **argv, agnc_cli_options_t *options)
{
    int index;

    if (options == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    memset(options, 0, sizeof(*options));

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

        if (strcmp(argv[index], "--no-tools") == 0) {
            options->no_tools = 1;
            continue;
        }

        if (strcmp(argv[index], "--yes") == 0 || strcmp(argv[index], "-y") == 0) {
            options->auto_approve = 1;
            continue;
        }

        if (strcmp(argv[index], "--print") == 0) {
            int prompt_index;

            options->show_print = 1;
            prompt_index = index + 1;
            while (prompt_index < argc && argv[prompt_index][0] == '-') {
                if (strcmp(argv[prompt_index], "--no-tools") == 0) {
                    options->no_tools = 1;
                    prompt_index++;
                    continue;
                }
                if (strcmp(argv[prompt_index], "--yes") == 0 || strcmp(argv[prompt_index], "-y") == 0) {
                    options->auto_approve = 1;
                    prompt_index++;
                    continue;
                }
                break;
            }

            if (prompt_index < argc) {
                options->print_prompt = agnc_strdup_local(argv[prompt_index]);
                if (options->print_prompt == NULL) {
                    return AGNC_STATUS_OUT_OF_MEMORY;
                }
                index = prompt_index;
            }
            continue;
        }

        fprintf(stderr, "agnc: unknown argument: %s\n", argv[index]);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!options->show_version && !options->show_doctor && !options->show_help && !options->show_print) {
        options->show_help = 1;
    }

    return AGNC_STATUS_OK;
}

int agnc_cli_run_version(void)
{
    printf("agnc %s\n", AGNC_VERSION_STRING);
    return 0;
}

int agnc_cli_run_help(void)
{
    printf("agnc - OpenClaude-compatible coding agent CLI (C)\n\n");
    printf("Usage:\n");
    printf("  agnc --version                 Show version\n");
    printf("  agnc doctor                    Check environment and dependencies\n");
    printf("  agnc --print \"your prompt\"     Run headless agent query (Phase 1)\n");
    printf("  agnc --print --no-tools \"...\"  Chat tanpa tool schema (model tanpa tool use)\n");
    printf("  agnc --print --yes \"...\"       Setujui shell otomatis (non-interaktif)\n");
    printf("  agnc --help                    Show this help\n");
    return 0;
}
