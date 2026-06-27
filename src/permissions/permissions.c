/*
 * permissions.c
 *
 * Prompt permission interaktif untuk eksekusi shell.
 */

#include "agnc/permissions.h"
#include "agnc/console.h"

#include <stdio.h>
#include <string.h>

static void agnc_permission_read_answer(int *allowed)
{
    char answer[32];

    if (allowed == NULL) {
        return;
    }

    *allowed = 0;
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return;
    }

    *allowed = (answer[0] == 'y' || answer[0] == 'Y');
}

agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed, int interactive_repl)
{
    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (interactive_repl) {
        agnc_console_print_permission_prompt("izinkan shell?", command);
    } else {
        fprintf(stderr, "agnc: [permission] allow shell? [y/N] %s\n", command != NULL ? command : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(allowed);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_permission_ask_file_write(
    const char *path,
    const char *operation,
    int *allowed,
    int interactive_repl)
{
    char label[64];

    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (interactive_repl) {
        snprintf(label, sizeof(label), "izinkan %s?", operation != NULL ? operation : "write");
        agnc_console_print_permission_prompt(label, path);
    } else {
        fprintf(
            stderr,
            "agnc: [permission] allow %s? [y/N] %s\n",
            operation != NULL ? operation : "write",
            path != NULL ? path : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(allowed);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_permission_ask_mcp(const char *tool_name, int *allowed, int interactive_repl)
{
    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (interactive_repl) {
        agnc_console_print_permission_prompt("izinkan MCP tool?", tool_name);
    } else {
        fprintf(
            stderr,
            "agnc: [permission] allow MCP tool? [y/N] %s\n",
            tool_name != NULL ? tool_name : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(allowed);
    return AGNC_STATUS_OK;
}
