/*
 * line_edit.c
 *
 * Line editing minimal untuk REPL (cursor, backspace, history).
 */

#include "agnc/line_edit.h"
#include "agnc/tui.h"
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
static agnc_repl_line_idle_needed_fn g_repl_line_idle_needed = NULL;
static agnc_repl_line_idle_poll_fn g_repl_line_idle_poll = NULL;
static agnc_repl_line_idle_needed_fn g_repl_line_idle_perm_needed = NULL;
static agnc_repl_line_idle_fn g_repl_line_idle_perm_handle = NULL;
static volatile int g_repl_line_exit_requested = 0;

void agnc_repl_line_reset_exit(void)
{
    g_repl_line_exit_requested = 0;
}

void agnc_repl_line_signal_exit(void)
{
    g_repl_line_exit_requested = 1;
}

void agnc_repl_line_set_idle(
    agnc_repl_line_idle_needed_fn needed,
    agnc_repl_line_idle_poll_fn poll,
    agnc_repl_line_idle_needed_fn perm_needed,
    agnc_repl_line_idle_fn perm_handle)
{
    g_repl_line_idle_needed = needed;
    g_repl_line_idle_poll = poll;
    g_repl_line_idle_perm_needed = perm_needed;
    g_repl_line_idle_perm_handle = perm_handle;
}

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

static SHORT agnc_repl_console_width(const CONSOLE_SCREEN_BUFFER_INFO *csbi)
{
    SHORT width;

    if (csbi == NULL) {
        return 80;
    }

    width = (SHORT)(csbi->srWindow.Right - csbi->srWindow.Left + 1);
    if (width > 0) {
        return width;
    }

    if (csbi->dwSize.X > 0) {
        return csbi->dwSize.X;
    }

    return 80;
}

static SHORT agnc_repl_draft_row_count(const agnc_repl_edit_view_t *view, const char *draft, size_t length)
{
    SHORT rows = 1;
    int col = (int)view->start.X;
    size_t index;

    if (length == 0) {
        return 1;
    }

    for (index = 0; index < length; index++) {
        if (draft[index] == '\n') {
            rows++;
            col = 0;
            continue;
        }
        col++;
        if (col >= view->screen_width) {
            rows++;
            col = 0;
        }
    }

    return rows;
}

static COORD agnc_repl_cursor_pos(const agnc_repl_edit_view_t *view, const char *draft, size_t cursor, COORD origin)
{
    COORD pos;
    size_t index;
    int col = (int)origin.X;
    SHORT row = origin.Y;

    pos = origin;

    for (index = 0; index < cursor && index < (draft != NULL ? strlen(draft) : 0); index++) {
        if (draft[index] == '\n') {
            row++;
            col = (int)origin.X;
            continue;
        }
        col++;
        if (col >= view->screen_width) {
            row++;
            col = (int)origin.X;
        }
    }

    if (agnc_tui_is_active() && row > view->start.Y) {
        row = view->start.Y;
        col = (int)origin.X;
    }

    pos.Y = row;
    pos.X = (SHORT)col;
    return pos;
}

static COORD agnc_repl_draft_origin(const agnc_repl_edit_view_t *view, SHORT total_rows)
{
    COORD origin = view->start;

    if (agnc_tui_is_active() && total_rows > 1) {
        origin.Y = (SHORT)(view->start.Y - (total_rows - 1));
        if (origin.Y < 0) {
            origin.Y = 0;
        }
    }

    return origin;
}

static COORD agnc_repl_draft_origin_for(const agnc_repl_edit_view_t *view, const char *draft, size_t length)
{
    SHORT rows = agnc_repl_draft_row_count(view, draft, length);

    if (agnc_tui_is_active() && rows > 1) {
        SHORT max_rows = (SHORT)(view->start.Y + 1);
        if (rows > max_rows) {
            rows = max_rows;
        }
    }

    return agnc_repl_draft_origin(view, rows);
}

static void agnc_repl_write_draft(const agnc_repl_edit_view_t *view, const char *draft, size_t length, COORD origin)
{
    COORD pos;
    DWORD written;
    size_t index;
    int col = (int)origin.X;

    pos = origin;
    SetConsoleCursorPosition(view->out, pos);

    for (index = 0; index < length; index++) {
        char ch = draft[index];

        if (ch == '\n') {
            pos.Y++;
            col = (int)origin.X;
            pos.X = origin.X;
            if (agnc_tui_is_active() && pos.Y > view->start.Y) {
                break;
            }
            SetConsoleCursorPosition(view->out, pos);
            continue;
        }

        WriteConsoleA(view->out, &ch, 1, &written, NULL);
        col++;
        if (col >= view->screen_width) {
            pos.Y++;
            col = (int)origin.X;
            pos.X = origin.X;
            if (agnc_tui_is_active() && pos.Y > view->start.Y) {
                break;
            }
            SetConsoleCursorPosition(view->out, pos);
        } else {
            pos.X = (SHORT)col;
        }
    }
}

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

static void agnc_repl_clear_rows(const agnc_repl_edit_view_t *view, COORD origin, SHORT row_count)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD fill_pos;
    DWORD written;
    SHORT row;
    SHORT end_y;

    if (row_count <= 0) {
        return;
    }

    if (!GetConsoleScreenBufferInfo(view->out, &csbi)) {
        return;
    }

    if (agnc_tui_is_active()) {
        end_y = (SHORT)(view->start.Y + 1);
    } else {
        end_y = (SHORT)(origin.Y + row_count);
        if (end_y > csbi.dwSize.Y) {
            end_y = csbi.dwSize.Y;
        }
    }

    for (row = 0; row < row_count; row++) {
        fill_pos.X = 0;
        fill_pos.Y = (SHORT)(origin.Y + row);
        if (fill_pos.Y >= end_y || fill_pos.Y >= csbi.dwSize.Y) {
            break;
        }
        FillConsoleOutputCharacterA(view->out, ' ', view->screen_width, fill_pos, &written);
        FillConsoleOutputAttribute(view->out, csbi.wAttributes, (DWORD)view->screen_width, fill_pos, &written);
    }
}

static void agnc_repl_paint_prompt_prefix(const agnc_repl_edit_view_t *view)
{
    COORD pos;
    DWORD written;
    const char prefix[] = "> ";

    if (!agnc_tui_is_active()) {
        return;
    }

    pos.X = 0;
    pos.Y = view->start.Y;
    SetConsoleCursorPosition(view->out, pos);
    WriteConsoleA(view->out, prefix, 2, &written, NULL);
}

static void agnc_repl_redraw(agnc_repl_edit_view_t *view, const char *draft, size_t length, size_t cursor)
{
    SHORT new_rows;
    SHORT clear_rows;
    COORD origin;

    new_rows = agnc_repl_draft_row_count(view, draft, length);
    if (agnc_tui_is_active() && new_rows > 1) {
        SHORT max_rows = (SHORT)(view->start.Y + 1);
        if (new_rows > max_rows) {
            new_rows = max_rows;
        }
    }
    clear_rows = view->rows_used > new_rows ? view->rows_used : new_rows;
    origin = agnc_repl_draft_origin(view, new_rows);

    agnc_repl_clear_rows(view, origin, clear_rows);
    agnc_repl_paint_prompt_prefix(view);
    agnc_repl_write_draft(view, draft, length, origin);

    view->rows_used = new_rows;
    SetConsoleCursorPosition(view->out, agnc_repl_cursor_pos(view, draft, cursor, origin));
}

static void agnc_repl_echo_char(
    agnc_repl_edit_view_t *view,
    const char *draft,
    char ch,
    size_t cursor_after,
    size_t length)
{
    COORD pos;
    COORD origin;
    DWORD written;

    if (cursor_after == 0) {
        return;
    }

    if (agnc_tui_is_active()) {
        agnc_repl_redraw(view, draft, length, cursor_after);
        return;
    }

    origin = agnc_repl_draft_origin_for(view, draft, length);
    pos = agnc_repl_cursor_pos(view, draft, cursor_after - 1, origin);
    SetConsoleCursorPosition(view->out, pos);
    WriteConsoleA(view->out, &ch, 1, &written, NULL);
    SetConsoleCursorPosition(view->out, agnc_repl_cursor_pos(view, draft, cursor_after, origin));
}

static void agnc_repl_echo_backspace_at_end(
    agnc_repl_edit_view_t *view,
    const char *draft,
    size_t cursor_after,
    size_t length)
{
    (void)cursor_after;
    (void)length;

    agnc_repl_redraw(view, draft, length, cursor_after);
}

static void agnc_repl_edit_init_view(agnc_repl_edit_view_t *view, HANDLE out)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int tui_row = 0;
    int tui_col = 0;

    memset(view, 0, sizeof(*view));
    view->out = out;
    view->rows_used = 1;

    agnc_tui_get_prompt_pos(&tui_row, &tui_col);
    if (tui_row > 0 && tui_col > 0) {
        view->start.X = (SHORT)(tui_col - 1);
        view->start.Y = (SHORT)(tui_row - 1);
        if (GetConsoleScreenBufferInfo(out, &csbi)) {
            view->screen_width = agnc_repl_console_width(&csbi);
        } else {
            view->screen_width = 80;
        }
        agnc_repl_paint_prompt_prefix(view);
        SetConsoleCursorPosition(out, view->start);
        return;
    }

    if (GetConsoleScreenBufferInfo(out, &csbi)) {
        view->start = csbi.dwCursorPosition;
        view->screen_width = agnc_repl_console_width(&csbi);
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

static int agnc_repl_key_ctrl_pressed(const INPUT_RECORD *record)
{
    return (record->Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
}

static int agnc_repl_is_word_char(char ch)
{
    return ch != '\0' && ch != ' ' && ch != '\t';
}

static size_t agnc_repl_word_left(const char *draft, size_t cursor)
{
    size_t pos = cursor;

    while (pos > 0 && !agnc_repl_is_word_char(draft[pos - 1])) {
        pos--;
    }
    while (pos > 0 && agnc_repl_is_word_char(draft[pos - 1])) {
        pos--;
    }
    return pos;
}

static size_t agnc_repl_word_right(const char *draft, size_t length, size_t cursor)
{
    size_t pos = cursor;

    while (pos < length && !agnc_repl_is_word_char(draft[pos])) {
        pos++;
    }
    while (pos < length && agnc_repl_is_word_char(draft[pos])) {
        pos++;
    }
    return pos;
}

static void agnc_repl_refresh_prompt_line(HANDLE stdout_handle, agnc_repl_edit_view_t *view)
{
    (void)stdout_handle;
    agnc_tui_show_prompt();
    agnc_repl_edit_init_view(view, stdout_handle);
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

        if (g_repl_line_exit_requested) {
            agnc_console_input_end(&input_session);
            return 0;
        }

        if (g_repl_line_idle_needed != NULL && g_repl_line_idle_needed()) {
            DWORD wait = WaitForSingleObject(stdin_handle, 50);

            if (wait == WAIT_TIMEOUT) {
                int layout_changed = 0;

                if (g_repl_line_idle_perm_needed != NULL && g_repl_line_idle_perm_needed()) {
                    agnc_console_input_end(&input_session);
                    if (g_repl_line_idle_perm_handle != NULL) {
                        g_repl_line_idle_perm_handle();
                    }
                    agnc_console_input_begin(&input_session);
                    layout_changed = 1;
                } else if (g_repl_line_idle_poll != NULL) {
                    layout_changed = g_repl_line_idle_poll();
                }
                if (layout_changed) {
                    agnc_repl_refresh_prompt_line(stdout_handle, &view);
                    agnc_repl_redraw(&view, draft, length, cursor);
                }
                continue;
            }

            if (wait != WAIT_OBJECT_0) {
                continue;
            }
        }

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
            if (agnc_repl_key_ctrl_pressed(&record)) {
                agnc_repl_insert_text(&view, draft, &length, &cursor, capacity, "\n");
                browsing = 0;
                continue;
            }
            if (!agnc_tui_is_active()) {
                agnc_console_input_echo_newline(&input_session);
            }
            break;
        }

        if (key == VK_ESCAPE) {
            draft[0] = '\0';
            length = 0;
            cursor = 0;
            browsing = 0;
            agnc_repl_redraw(&view, draft, length, cursor);
            continue;
        }

        if (key == VK_BACK) {
            if (agnc_repl_key_ctrl_pressed(&record)) {
                if (cursor > 0) {
                    size_t new_cursor = agnc_repl_word_left(draft, cursor);

                    memmove(draft + new_cursor, draft + cursor, length - cursor + 1);
                    length -= cursor - new_cursor;
                    cursor = new_cursor;
                    agnc_repl_redraw(&view, draft, length, cursor);
                }
            } else if (cursor > 0) {
                if (cursor == length && length > 1) {
                    length--;
                    cursor--;
                    draft[length] = '\0';
                    agnc_repl_echo_backspace_at_end(&view, draft, cursor, length);
                } else if (cursor == length && length == 1) {
                    length = 0;
                    cursor = 0;
                    draft[0] = '\0';
                    agnc_repl_redraw(&view, draft, length, cursor);
                } else {
                    memmove(draft + cursor - 1, draft + cursor, length - cursor + 1);
                    cursor--;
                    length--;
                    agnc_repl_redraw(&view, draft, length, cursor);
                }
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
            if (agnc_repl_key_ctrl_pressed(&record)) {
                cursor = agnc_repl_word_left(draft, cursor);
            } else if (cursor > 0) {
                cursor--;
            }
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(
                &view,
                draft,
                cursor,
                agnc_repl_draft_origin_for(&view, draft, length)));
            continue;
        }

        if (key == VK_RIGHT) {
            if (agnc_repl_key_ctrl_pressed(&record)) {
                cursor = agnc_repl_word_right(draft, length, cursor);
            } else if (cursor < length) {
                cursor++;
            }
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(
                &view,
                draft,
                cursor,
                agnc_repl_draft_origin_for(&view, draft, length)));
            continue;
        }

        if (key == VK_HOME) {
            cursor = 0;
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(
                &view,
                draft,
                cursor,
                agnc_repl_draft_origin_for(&view, draft, length)));
            continue;
        }

        if (key == VK_END) {
            cursor = length;
            SetConsoleCursorPosition(view.out, agnc_repl_cursor_pos(
                &view,
                draft,
                cursor,
                agnc_repl_draft_origin_for(&view, draft, length)));
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
            g_repl_line_exit_requested = 1;
            agnc_console_input_end(&input_session);
            return 0;
        }

        if (agnc_console_input_key_printable(
                record.Event.KeyEvent.dwControlKeyState,
                record.Event.KeyEvent.wVirtualKeyCode,
                record.Event.KeyEvent.uChar.AsciiChar) &&
            length + 1 < capacity && length + 1 < sizeof(draft)) {
            char ch = record.Event.KeyEvent.uChar.AsciiChar;
            SHORT rows_before;
            int append_at_end = cursor == length;
            int has_newline = strchr(draft, '\n') != NULL;

            if (cursor < length) {
                memmove(draft + cursor + 1, draft + cursor, length - cursor + 1);
            }
            draft[cursor] = ch;
            cursor++;
            length++;
            draft[length] = '\0';

            rows_before = agnc_repl_draft_row_count(&view, draft, length - 1);

            if (append_at_end && !has_newline && ch != '\n' &&
                rows_before == agnc_repl_draft_row_count(&view, draft, length) &&
                rows_before == 1) {
                agnc_repl_echo_char(&view, draft, ch, cursor, length);
                view.rows_used = 1;
            } else {
                agnc_repl_redraw(&view, draft, length, cursor);
            }
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
