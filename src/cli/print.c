/*
 * print.c
 *
 * Subcommand headless --print untuk Fase 1 spike OpenRouter.
 */

#include "agnc/cli.h"
#include "agnc/config.h"
#include "agnc/query.h"

#include <stdio.h>
#include <stdlib.h>

int agnc_cli_run_print(const char *prompt, int no_tools)
{
    agnc_config_t config;
    agnc_status_t status;

    if (prompt == NULL || prompt[0] == '\0') {
        fprintf(stderr, "agnc: --print requires a prompt\n");
        return 1;
    }

    agnc_config_init(&config);
    status = agnc_config_load(NULL, &config);
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: failed to load config (~/.agnc.json): %s\n", agnc_status_to_string(status));
        fprintf(stderr, "agnc: copy config\\agnc.example.json to %%USERPROFILE%%\\.agnc.json and set API key env\n");
        agnc_config_free(&config);
        return 1;
    }

    /*
     * --print memaksa non-stream: jawaban message.content utuh per turn.
     * Streaming delta masih bermasalah pada sebagian model (teks kumulatif per chunk).
     */
    config.stream = 0;

    if (no_tools) {
        config.enable_tools = 0;
        config.tool_read_file = 0;
        config.tool_shell = 0;
    }

    status = agnc_query_print(&config, prompt);
    agnc_config_free(&config);

    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: query failed: %s\n", agnc_status_to_string(status));
        return 1;
    }

    return 0;
}
