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

static agnc_permission_background_ask_fn g_background_ask = NULL;
static void *g_background_ask_ctx = NULL;

void agnc_permission_set_background_ask(agnc_permission_background_ask_fn fn, void *ctx)
{
    g_background_ask = fn;
    g_background_ask_ctx = ctx;
}

void agnc_permission_clear_background_ask(void)
{
    g_background_ask = NULL;
    g_background_ask_ctx = NULL;
}

int agnc_permission_session_has_shell(void)
{
    return agnc_permission_session_has(AGNC_PERM_SESSION_SHELL);
}

static int agnc_permission_ask_non_interactive(
    unsigned session_flag,
    const char *kind,
    const char *detail,
    int *allowed)
{
    if (allowed == NULL) {
        return 0;
    }

    if (agnc_permission_session_has(session_flag)) {
        *allowed = 1;
        return 1;
    }

    if (g_background_ask != NULL) {
        *allowed = g_background_ask(kind, detail, g_background_ask_ctx) ? 1 : 0;
        if (*allowed) {
            agnc_permission_session_grant(session_flag);
        }
        return 1;
    }

    *allowed = 0;
    fprintf(
        stderr,
        "agnc: [permission] denied (non-interactive): %s %s\n",
        kind != NULL ? kind : "?",
        detail != NULL ? detail : "(empty)");
    fflush(stderr);
    return 1;
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
        agnc_permission_read_answer(AGNC_PERM_SESSION_SHELL, "shell", interactive_repl, allowed);
        return AGNC_STATUS_OK;
    }

    if (agnc_permission_ask_non_interactive(AGNC_PERM_SESSION_SHELL, "shell", command, allowed)) {
        return AGNC_STATUS_OK;
    }

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
        agnc_permission_read_answer(AGNC_PERM_SESSION_WRITE, "write/edit file", interactive_repl, allowed);
        return AGNC_STATUS_OK;
    }

    if (agnc_permission_ask_non_interactive(AGNC_PERM_SESSION_WRITE, operation != NULL ? operation : "write", path, allowed)) {
        return AGNC_STATUS_OK;
    }

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
        agnc_permission_read_answer(AGNC_PERM_SESSION_MCP, "MCP tools", interactive_repl, allowed);
        return AGNC_STATUS_OK;
    }

    if (agnc_permission_ask_non_interactive(AGNC_PERM_SESSION_MCP, "mcp", tool_name, allowed)) {
        return AGNC_STATUS_OK;
    }

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
        agnc_permission_read_answer(AGNC_PERM_SESSION_WEB_FETCH, "web_fetch", interactive_repl, allowed);
        return AGNC_STATUS_OK;
    }

    if (agnc_permission_ask_non_interactive(AGNC_PERM_SESSION_WEB_FETCH, "web_fetch", url, allowed)) {
        return AGNC_STATUS_OK;
    }

    return AGNC_STATUS_OK;
}
