/*
 * print.c
 *
 * Subcommand headless --print: muat config, terapkan override CLI, jalankan agent loop.
 */

#include "agnc/cli.h"
#include "agnc/config.h"
#include "agnc/query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int agnc_cli_run_print(const char *prompt, int no_tools, int auto_approve)
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
        config.tool_write_file = 0;
        config.tool_edit_file = 0;
        config.tool_grep = 0;
        config.tool_glob = 0;
        config.tool_web_fetch = 0;
        config.tool_todo_write = 0;
    }

    if (auto_approve) {
        config.ask_shell_permission = 0;
        config.ask_write_permission = 0;
        config.ask_mcp_permission = 0;
        config.ask_web_fetch_permission = 0;
    }

    {
        agnc_query_options_t options;
        memset(&options, 0, sizeof(options));
        options.auto_approve = auto_approve;
        status = agnc_query_print(&config, prompt, &options);
    }
    agnc_config_free(&config);

    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: query failed: %s\n", agnc_status_to_string(status));
        return 1;
    }

    return 0;
}
