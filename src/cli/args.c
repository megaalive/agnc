/*
 * args.c
 *
 * Parser argumen CLI dan subcommand dasar (--version, --help, --print).
 * Implementasi diekspor sebagai agnc_cli_*_impl; pemanggil memakai wrapper
 * static inline di include/agnc/cli.h (menghindari saran static IntelliSense VCR003).
 */

#include "agnc/cli.h"
#include "agnc/export.h"
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

/* Bebaskan memori hasil agnc_cli_parse (--print prompt, dll.). */
AGNC_API void agnc_cli_options_free_impl(agnc_cli_options_t *options)
{
    if (options == NULL) {
        return;
    }

    free(options->print_prompt);
    options->print_prompt = NULL;
    free(options->models_provider_filter);
    options->models_provider_filter = NULL;
    free(options->models_name_filter);
    options->models_name_filter = NULL;
}

/* Parse argv ke agnc_cli_options_t; default ke --help jika tidak ada subcommand. */
AGNC_API agnc_status_t agnc_cli_parse_impl(int argc, char **argv, agnc_cli_options_t *options)
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

        if (strcmp(argv[index], "models") == 0) {
            options->show_models = 1;
            for (index = index + 1; index < argc; index++) {
                if (strcmp(argv[index], "--json") == 0) {
                    options->models_json = 1;
                    continue;
                }
                if (strcmp(argv[index], "--filter") == 0 || strcmp(argv[index], "-f") == 0) {
                    if (index + 1 >= argc) {
                        fprintf(stderr, "agnc: --filter requires a pattern\n");
                        return AGNC_STATUS_INVALID_ARGUMENT;
                    }
                    free(options->models_name_filter);
                    options->models_name_filter = agnc_strdup_local(argv[index + 1]);
                    if (options->models_name_filter == NULL) {
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }
                    index++;
                    continue;
                }
                if (argv[index][0] == '-') {
                    fprintf(stderr, "agnc: unknown argument: %s\n", argv[index]);
                    return AGNC_STATUS_INVALID_ARGUMENT;
                }
                if (options->models_provider_filter == NULL) {
                    options->models_provider_filter = agnc_strdup_local(argv[index]);
                    if (options->models_provider_filter == NULL) {
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }
                } else if (options->models_name_filter == NULL) {
                    options->models_name_filter = agnc_strdup_local(argv[index]);
                    if (options->models_name_filter == NULL) {
                        return AGNC_STATUS_OUT_OF_MEMORY;
                    }
                } else {
                    fprintf(stderr, "agnc: too many model list arguments (use --filter)\n");
                    return AGNC_STATUS_INVALID_ARGUMENT;
                }
            }
            index--;
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

    if (!options->show_version && !options->show_doctor && !options->show_help && !options->show_print &&
        !options->show_models) {
        options->show_interactive = 1;
    }

    return AGNC_STATUS_OK;
}

AGNC_API int agnc_cli_run_version_impl(void)
{
    printf("agnc %s\n", AGNC_VERSION_STRING);
    return 0;
}

/* Tampilkan usage singkat ke stdout. */
AGNC_API int agnc_cli_run_help_impl(void)
{
    printf("agnc - personal coding-agent CLI (C)\n\n");
    printf("Usage:\n");
    printf("  agnc                           Interactive REPL (default)\n");
    printf("  agnc --version                 Show version\n");
    printf("  agnc doctor                    Check environment and dependencies\n");
    printf("  agnc models [provider] [filter]  List models (optional name filter)\n");
    printf("  agnc models --filter PATTERN     Filter by substring (case-insensitive)\n");
    printf("  agnc models --json               JSON output (all providers)\n");
    printf("  agnc --print \"your prompt\"     Run headless agent query (Phase 1)\n");
    printf("  agnc --print --no-tools \"...\"  Chat tanpa tool schema (model tanpa tool use)\n");
    printf("  agnc --print --yes \"...\"       Setujui shell otomatis (non-interaktif)\n");
    printf("  agnc --help                    Show this help\n");
    return 0;
}
