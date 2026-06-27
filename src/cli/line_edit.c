/*
 * line_edit.c
 *
 * Line editing minimal untuk REPL (cursor, backspace, history).
 */

#include "agnc/line_edit.h"
#include "agnc/console.h"

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

typedef struct {
    HANDLE out;
    COORD start;
    SHORT screen_width;
    SHORT rows_used;
} agnc_repl_edit_view_t;

static SHORT agnc_repl_rows_for_length(SHORT start_col, size_t length, SHORT screen_width)
{
    size_t first_row;

    if (screen_width <= 0) {
        return 1;
    }

    if (length == 0) {
        return 1;
    }

    first_row = (size_t)screen_width - (size_t)start_col;
    if (length <= first_row) {
        return 1;
    }

    return (SHORT)(1 + (length - first_row + (size_t)screen_width - 1) / (size_t)screen_width);
}

static COORD agnc_repl_cursor_pos(const agnc_repl_edit_view_t *view, size_t cursor)
{
    COORD pos;
    int linear;

    linear = (int)view->start.X + (int)cursor;
    pos.Y = (SHORT)(view->start.Y + linear / view->screen_width);
    pos.X = (SHORT)(linear % view->screen_width);
    return pos;
}

static void agnc_repl_clear_rows(const agnc_repl_edit_view_t *view, SHORT row_count)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD fill_pos;
    DWORD written;
    SHORT row;

    if (row_count <= 0) {
        return;
    }

    if (!GetConsoleScreenBufferInfo(view->out, &csbi)) {
        return;
    }

    for (row = 0; row < row_count; row++) {
        fill_pos.X = 0;
        fill_pos.Y = (SHORT)(view->start.Y + row);
        if (fill_pos.Y >= csbi.dwSize.Y) {
            break;
        }
        FillConsoleOutputCharacterA(view->out, ' ', view->screen_width, fill_pos, &written);
        FillConsoleOutputAttribute(view->out, csbi.wAttributes, (DWORD)view->screen_width, fill_pos, &written);
    }
}

static void agnc_repl_redraw(agnc_repl_edit_view_t *view, const char *draft, size_t length, size_t cursor)
{
    DWORD written;
    SHORT new_rows;
    SHORT clear_rows;

    new_rows = agnc_repl_rows_for_length(view->start.X, length, view->screen_width);
    clear_rows = view->rows_used > new_rows ? view->rows_used : new_rows;

    agnc_repl_clear_rows(view, clear_rows);
    SetConsoleCursorPosition(view->out, view->start);
    if (length > 0) {
        WriteConsoleA(view->out, draft, (DWORD)length, &written, NULL);
    }

    view->rows_used = new_rows;
    SetConsoleCursorPosition(view->out, agnc_repl_cursor_pos(view, cursor));
}

static void agnc_repl_edit_init_view(agnc_repl_edit_view_t *view, HANDLE out)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    memset(view, 0, sizeof(*view));
    view->out = out;
    view->rows_used = 1;

    if (GetConsoleScreenBufferInfo(out, &csbi)) {
        view->start = csbi.dwCursorPosition;
        view->screen_width = csbi.dwSize.X;
        if (view->screen_width <= 0) {
            view->screen_width = 80;
        }
    } else {
        view->start.X = 0;
        view->start.Y = 0;
        view->screen_width = 80;
    }
}

static void agnc_repl_insert_text(
    agnc_repl_edit_view_t *view,
    char *draft,
    size_t *length,
    size_t *cursor,
    size_t capacity,
    const char *text)
{
    size_t text_len;
    size_t available;

    if (text == NULL || text[0] == '\0') {
        return;
    }

    text_len = strlen(text);
    if (text_len == 0) {
        return;
    }

    available = capacity - 1;
    if (*length >= available) {
        return;
    }

    if (text_len > available - *length) {
        text_len = available - *length;
    }

    if (*cursor < *length) {
        memmove(draft + *cursor + text_len, draft + *cursor, *length - *cursor + 1);
    } else {
        draft[*length + text_len] = '\0';
    }

    memcpy(draft + *cursor, text, text_len);
    *cursor += text_len;
    *length += text_len;
    draft[*length] = '\0';
    agnc_repl_redraw(view, draft, *length, *cursor);
}

int agnc_repl_read_line(char *buffer, size_t capacity)
{
    HANDLE stdin_handle;
    HANDLE stdout_handle;
    agnc_console_input_session_t input_session;
    agnc_repl_edit_view_t view;
    size_t length = 0;
    size_t cursor = 0;
    char draft[AGNC_REPL_HISTORY_WIDTH];
    int browsing = 0;

    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    buffer[0] = '\0';
    draft[0] = '\0';

    stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdin_handle == INVALID_HANDLE_VALUE || stdout_handle == INVALID_HANDLE_VALUE) {
        return fgets(buffer, (int)capacity, stdin) != NULL;
    }

    agnc_console_input_prepare_repl();
    agnc_console_input_begin(&input_session);

    agnc_repl_edit_init_view(&view, stdout_handle);
    g_repl_history_browse = g_repl_history_count;

    for (;;) {
        INPUT_RECORD record;
        DWORD read_count;
        WORD key;

        if (!ReadConsoleInputA(stdin_handle, &record, 1, &read_count) || read_count == 0) {
            agnc_console_input_end(&input_session);
            return length > 0;
        }

        if (record.EventType == MOUSE_EVENT) {
            continue;
        }

        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }

        key = record.Event.KeyEvent.wVirtualKeyCode;

        if (agnc_console_input_is_paste_key(
                record.Event.KeyEvent.dwControlKeyState,
                key,
                record.Event.KeyEvent.uChar.AsciiChar)) {
            char paste_buf[512];
            size_t pasted = agnc_console_input_paste_clipboard(paste_buf, sizeof(paste_buf));

            if (pasted > 0) {
                agnc_repl_insert_text(&view, draft, &length, &cursor, capacity, paste_buf);
                browsing = 0;
            }
            continue;
        }

        if (key == VK_RETURN) {
            agnc_console_input_echo_newline(&input_session);
            break;
        }

        if (key == VK_BACK) {
            if (cursor > 0) {
                memmove(draft + cursor - 1, draft + cursor, length - cursor + 1);
                cursor--;
                length--;
                agnc_repl_redraw(&view, draft, length, cursor);
            }
            browsing = 0;
            continue;
        }

        if (key == VK_DELETE) {
            if (cursor < length) {
                memmove(draft + cursor, draft + cursor + 1, length - cursor);
                length--;
                agnc_repl_redraw(&view, draft, length, cursor);
            }
            browsing = 0;
            continue;
        }

        if (key == VK_LEFT) {
            if (cursor > 0) {
                cursor--;
                SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(&view, cursor));
            }
            continue;
        }

        if (key == VK_RIGHT) {
            if (cursor < length) {
                cursor++;
                SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(&view, cursor));
            }
            continue;
        }

        if (key == VK_HOME) {
            cursor = 0;
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(&view, cursor));
            continue;
        }

        if (key == VK_END) {
            cursor = length;
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(&view, cursor));
            continue;
        }

        if (key == VK_UP) {
            if (g_repl_history_count > 0 && g_repl_history_browse > 0) {
                g_repl_history_browse--;
                snprintf(draft, sizeof(draft), "%s", g_repl_history[g_repl_history_browse]);
                length = strlen(draft);
                cursor = length;
                agnc_repl_redraw(&view, draft, length, cursor);
                browsing = 1;
            }
            continue;
        }

        if (key == VK_DOWN) {
            if (g_repl_history_browse + 1 < g_repl_history_count) {
                g_repl_history_browse++;
                snprintf(draft, sizeof(draft), "%s", g_repl_history[g_repl_history_browse]);
                length = strlen(draft);
                cursor = length;
                agnc_repl_redraw(&view, draft, length, cursor);
                browsing = 1;
            } else if (browsing) {
                g_repl_history_browse = g_repl_history_count;
                draft[0] = '\0';
                length = 0;
                cursor = 0;
                agnc_repl_redraw(&view, draft, length, cursor);
                browsing = 0;
            }
            continue;
        }

        if (record.Event.KeyEvent.uChar.AsciiChar == 3) {
            agnc_console_input_end(&input_session);
            return 0;
        }

        if (agnc_console_input_key_printable(
                record.Event.KeyEvent.dwControlKeyState,
                record.Event.KeyEvent.wVirtualKeyCode,
                record.Event.KeyEvent.uChar.AsciiChar) &&
            length + 1 < capacity && length + 1 < sizeof(draft)) {
            char ch = record.Event.KeyEvent.uChar.AsciiChar;

            if (cursor < length) {
                memmove(draft + cursor + 1, draft + cursor, length - cursor + 1);
            }
            draft[cursor] = ch;
            cursor++;
            length++;
            draft[length] = '\0';
            agnc_repl_redraw(&view, draft, length, cursor);
            browsing = 0;
        }
    }

    agnc_console_input_end(&input_session);
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
