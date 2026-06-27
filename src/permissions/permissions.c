/*
 * permissions.c
 *
 * Prompt permission interaktif untuk eksekusi shell.
 */

#include "agnc/permissions.h"
#include "agnc/console.h"

#include <stdio.h>
#include <string.h>

#define AGNC_PERM_SESSION_SHELL 1u
#define AGNC_PERM_SESSION_WRITE 2u
#define AGNC_PERM_SESSION_MCP 4u
#define AGNC_PERM_SESSION_WEB_FETCH 8u

static unsigned g_perm_session_granted = 0;

void agnc_permission_session_reset(void)
{
    g_perm_session_granted = 0;
}

static int agnc_permission_session_has(unsigned flag)
{
    return (g_perm_session_granted & flag) != 0;
}

static void agnc_permission_session_grant(unsigned flag)
{
    g_perm_session_granted |= flag;
}

static void agnc_permission_read_answer(unsigned session_flag, const char *session_label, int interactive_repl, int *allowed)
{
    int was_granted;

    if (allowed == NULL) {
        return;
    }

    was_granted = agnc_permission_session_has(session_flag);
    agnc_console_read_yes_no(allowed);

    if (*allowed) {
        agnc_permission_session_grant(session_flag);
        if (interactive_repl && !was_granted && session_label != NULL) {
            char detail[96];
            snprintf(detail, sizeof(detail), "%s diizinkan untuk sisa sesi REPL", session_label);
            agnc_console_print_chat_system(detail);
        }
    }
}

agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed, int interactive_repl)
{
    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (agnc_permission_session_has(AGNC_PERM_SESSION_SHELL)) {
        *allowed = 1;
        return AGNC_STATUS_OK;
    }

    if (interactive_repl) {
        agnc_console_print_permission_prompt("izinkan shell?", command);
    } else {
        fprintf(stderr, "agnc: [permission] allow shell? [y/N] %s\n", command != NULL ? command : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(AGNC_PERM_SESSION_SHELL, "shell", interactive_repl, allowed);
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

    if (agnc_permission_session_has(AGNC_PERM_SESSION_WRITE)) {
        *allowed = 1;
        return AGNC_STATUS_OK;
    }

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

    agnc_permission_read_answer(AGNC_PERM_SESSION_WRITE, "write/edit file", interactive_repl, allowed);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_permission_ask_mcp(const char *tool_name, int *allowed, int interactive_repl)
{
    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (agnc_permission_session_has(AGNC_PERM_SESSION_MCP)) {
        *allowed = 1;
        return AGNC_STATUS_OK;
    }

    if (interactive_repl) {
        agnc_console_print_permission_prompt("izinkan MCP tool?", tool_name);
    } else {
        fprintf(
            stderr,
            "agnc: [permission] allow MCP tool? [y/N] %s\n",
            tool_name != NULL ? tool_name : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(AGNC_PERM_SESSION_MCP, "MCP tools", interactive_repl, allowed);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_permission_ask_web_fetch(const char *url, int *allowed, int interactive_repl)
{
    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;
    agnc_console_spinner_stop();

    if (agnc_permission_session_has(AGNC_PERM_SESSION_WEB_FETCH)) {
        *allowed = 1;
        return AGNC_STATUS_OK;
    }

    if (interactive_repl) {
        agnc_console_print_permission_prompt("izinkan web_fetch?", url);
    } else {
        fprintf(
            stderr,
            "agnc: [permission] allow web_fetch? [y/N] %s\n",
            url != NULL ? url : "(empty)");
        fflush(stderr);
    }

    agnc_permission_read_answer(AGNC_PERM_SESSION_WEB_FETCH, "web_fetch", interactive_repl, allowed);
    return AGNC_STATUS_OK;
}
