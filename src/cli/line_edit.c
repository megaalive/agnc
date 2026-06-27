/*
 * line_edit.c
 *
 * Line editing minimal untuk REPL (backspace + history 32 baris).
 */

#include "agnc/line_edit.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define AGNC_REPL_HISTORY_MAX 32
#define AGNC_REPL_HISTORY_WIDTH 512

static char g_repl_history[AGNC_REPL_HISTORY_MAX][AGNC_REPL_HISTORY_WIDTH];
static size_t g_repl_history_count = 0;
static size_t g_repl_history_browse = 0;

static void agnc_repl_history_push(const char *line)
{
    size_t index;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    if (g_repl_history_count > 0 &&
        strcmp(g_repl_history[g_repl_history_count - 1], line) == 0) {
        return;
    }

    if (g_repl_history_count < AGNC_REPL_HISTORY_MAX) {
        snprintf(g_repl_history[g_repl_history_count], AGNC_REPL_HISTORY_WIDTH, "%s", line);
        g_repl_history_count++;
    } else {
        for (index = 1; index < AGNC_REPL_HISTORY_MAX; index++) {
            memcpy(g_repl_history[index - 1], g_repl_history[index], AGNC_REPL_HISTORY_WIDTH);
        }
        snprintf(g_repl_history[AGNC_REPL_HISTORY_MAX - 1], AGNC_REPL_HISTORY_WIDTH, "%s", line);
    }

    g_repl_history_browse = g_repl_history_count;
}

#ifdef _WIN32
static void agnc_repl_render_line(const char *text, size_t length)
{
    printf("\r%*s\r", 80, "");
    fwrite(text, 1, length, stdout);
    fflush(stdout);
}

int agnc_repl_read_line(char *buffer, size_t capacity)
{
    HANDLE stdin_handle;
    DWORD mode;
    size_t length = 0;
    char draft[AGNC_REPL_HISTORY_WIDTH];
    int browsing = 0;

    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    buffer[0] = '\0';
    draft[0] = '\0';

    stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_handle == INVALID_HANDLE_VALUE) {
        return fgets(buffer, (int)capacity, stdin) != NULL;
    }

    GetConsoleMode(stdin_handle, &mode);
    SetConsoleMode(stdin_handle, mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    g_repl_history_browse = g_repl_history_count;

    for (;;) {
        INPUT_RECORD record;
        DWORD read_count;

        if (!ReadConsoleInputA(stdin_handle, &record, 1, &read_count) || read_count == 0) {
            SetConsoleMode(stdin_handle, mode);
            return length > 0;
        }

        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }

        if (record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
            printf("\n");
            break;
        }

        if (record.Event.KeyEvent.wVirtualKeyCode == VK_BACK) {
            if (length > 0) {
                length--;
                draft[length] = '\0';
                agnc_repl_render_line(draft, length);
            }
            browsing = 0;
            continue;
        }

        if (record.Event.KeyEvent.wVirtualKeyCode == VK_UP) {
            if (g_repl_history_count > 0 && g_repl_history_browse > 0) {
                g_repl_history_browse--;
                snprintf(draft, sizeof(draft), "%s", g_repl_history[g_repl_history_browse]);
                length = strlen(draft);
                agnc_repl_render_line(draft, length);
                browsing = 1;
            }
            continue;
        }

        if (record.Event.KeyEvent.wVirtualKeyCode == VK_DOWN) {
            if (g_repl_history_browse + 1 < g_repl_history_count) {
                g_repl_history_browse++;
                snprintf(draft, sizeof(draft), "%s", g_repl_history[g_repl_history_browse]);
                length = strlen(draft);
                agnc_repl_render_line(draft, length);
                browsing = 1;
            } else if (browsing) {
                g_repl_history_browse = g_repl_history_count;
                draft[0] = '\0';
                length = 0;
                agnc_repl_render_line(draft, length);
                browsing = 0;
            }
            continue;
        }

        if (record.Event.KeyEvent.uChar.AsciiChar == 3) {
            SetConsoleMode(stdin_handle, mode);
            return 0;
        }

        if (record.Event.KeyEvent.uChar.AsciiChar >= 32 && length + 1 < capacity &&
            length + 1 < sizeof(draft)) {
            draft[length++] = record.Event.KeyEvent.uChar.AsciiChar;
            draft[length] = '\0';
            agnc_repl_render_line(draft, length);
            browsing = 0;
        }
    }

    SetConsoleMode(stdin_handle, mode);
    snprintf(buffer, capacity, "%s", draft);
    agnc_repl_history_push(buffer);
    return 1;
}
#else
int agnc_repl_read_line(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    if (fgets(buffer, (int)capacity, stdin) == NULL) {
        return 0;
    }

    {
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[--len] = '\0';
        }
    }

    agnc_repl_history_push(buffer);
    return 1;
}
#endif
