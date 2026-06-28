/*
 * tui.c
 *
 * Layout REPL: scroll region + input/status/panel di bawah.
 * Keputusan 6.23.5: ANSI VT murni (Windows ENABLE_VIRTUAL_TERMINAL_PROCESSING).
 */
#include "agnc/tui.h"
#include "agnc/console.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define AGNC_TUI_TOOL_LOG_MAX 8
#define AGNC_TUI_TOOL_LINE_MAX 120
#define AGNC_TUI_PANEL_LINES 3
#define AGNC_TUI_MIN_ROWS 8
static int g_tui_enabled = 0;
static int g_tui_active = 0;
static int g_tui_rows = 24;
static int g_tui_cols = 80;
static int g_tui_viewport_bottom = 23;
static int g_tui_scroll_bottom = 22;
static int g_tui_input_row = 23;
static int g_tui_status_row = 24;
static int g_tui_panel_start = 0;
static int g_tui_scroll_lock = 0;
static int g_tui_chat_emit = 0;
static int g_tui_chrome_dirty = 0;
static int g_tui_chat_row = 1;
static char g_tui_status_cached[512];
static volatile int g_tui_spinner = 0;
static int g_tui_spinner_frame = 0;
static agnc_tui_view_mode_t g_tui_view = AGNC_TUI_VIEW_NORMAL;
static char g_model[128];
static char g_session[64];
static long g_last_turn_tokens = -1;
static int g_queue_jobs = 0;
static int g_running_jobs = 0;
static char g_toast[160];
static char g_tool_log[AGNC_TUI_TOOL_LOG_MAX][AGNC_TUI_TOOL_LINE_MAX];
static size_t g_tool_log_count = 0;
static time_t g_toast_until = 0;
static void agnc_tui_query_size(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    g_tui_rows = 24;
    g_tui_cols = 80;
    if (out != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(out, &csbi)) {
        g_tui_viewport_bottom = (int)csbi.srWindow.Bottom;
        g_tui_rows = (int)csbi.srWindow.Bottom - (int)csbi.srWindow.Top + 1;
        g_tui_cols = (int)csbi.srWindow.Right - (int)csbi.srWindow.Left + 1;
        if (g_tui_cols < 40) {
            g_tui_cols = (int)csbi.dwSize.X;
        }
    }
#else
    g_tui_rows = 24;
    g_tui_cols = 80;
#endif
    if (g_tui_rows < AGNC_TUI_MIN_ROWS) {
        g_tui_rows = AGNC_TUI_MIN_ROWS;
    }
    if (g_tui_cols < 40) {
        g_tui_cols = 80;
    }
}
static int agnc_tui_footer_lines(void)
{
    int footer = 2;
    if (g_tui_view != AGNC_TUI_VIEW_NORMAL) {
        footer += AGNC_TUI_PANEL_LINES;
    }
    return footer;
}
static void agnc_tui_compute_layout(void)
{
    int footer = agnc_tui_footer_lines();
    agnc_tui_query_size();
    g_tui_status_row = g_tui_viewport_bottom + 1;
    g_tui_scroll_bottom = g_tui_status_row - footer;
    if (g_tui_scroll_bottom < 4) {
        g_tui_scroll_bottom = 4;
    }
    if (g_tui_view != AGNC_TUI_VIEW_NORMAL) {
        g_tui_panel_start = g_tui_status_row - footer + 1;
        g_tui_input_row = g_tui_panel_start + AGNC_TUI_PANEL_LINES;
    } else {
        g_tui_panel_start = 0;
        g_tui_input_row = g_tui_status_row - 1;
    }
    if (g_tui_input_row >= g_tui_status_row) {
        g_tui_input_row = g_tui_status_row - 1;
    }
    if (g_tui_input_row < 1) {
        g_tui_input_row = 1;
    }
    if (g_tui_status_row < g_tui_input_row + 1) {
        g_tui_status_row = g_tui_input_row + 1;
    }
}
static void agnc_tui_apply_scroll_region(void)
{
    agnc_tui_compute_layout();
#ifdef _WIN32
    /* DECSTBM + Win32 campur → kursor desync; scroll pakai ScrollConsoleScreenBuffer. */
    return;
#endif
    printf("\033[1;%dr", g_tui_scroll_bottom);
    fflush(stdout);
}
static void agnc_tui_reset_scroll_region(void)
{
    printf("\033[r");
    fflush(stdout);
}
static void agnc_tui_goto_row(int row)
{
    if (row < 1) {
        row = 1;
    }
    printf("\033[%d;1H", row);
}
static void agnc_tui_clear_line(int row)
{
    agnc_tui_goto_row(row);
    printf("\033[2K");
}
static void agnc_tui_set_cursor_visible(int visible)
{
    if (visible) {
        printf("\033[?25h");
    } else {
        printf("\033[?25l");
    }
    fflush(stdout);
}
#ifdef _WIN32
static void agnc_tui_fill_spaces_win32(HANDLE out, COORD pos, DWORD count, WORD attr)
{
    DWORD written;

    if (out == INVALID_HANDLE_VALUE || count == 0) {
        return;
    }

    FillConsoleOutputCharacterW(out, L' ', count, pos, &written);
    FillConsoleOutputAttribute(out, attr, count, pos, &written);
}

static void agnc_tui_clear_row_win32(int row, WORD attr)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos;

    if (out == INVALID_HANDLE_VALUE || row < 1) {
        return;
    }

    pos.X = 0;
    pos.Y = (SHORT)(row - 1);
    agnc_tui_fill_spaces_win32(out, pos, (DWORD)g_tui_cols, attr);
}

static void agnc_tui_paint_row_win32(int row, const char *text, WORD attr)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD pos;
    COORD saved;
    DWORD written;
    char line[512];
    size_t len;
    if (out == INVALID_HANDLE_VALUE || text == NULL || !GetConsoleScreenBufferInfo(out, &csbi)) {
        return;
    }
    saved = csbi.dwCursorPosition;
    snprintf(line, sizeof(line), "%.*s", g_tui_cols - 1, text);
    len = strlen(line);
    pos.X = 0;
    pos.Y = (SHORT)(row - 1);
    if (pos.Y < 0) {
        return;
    }
    FillConsoleOutputCharacterW(out, L' ', (DWORD)g_tui_cols, pos, &written);
    FillConsoleOutputAttribute(out, attr, (DWORD)g_tui_cols, pos, &written);
    if (len > 0) {
        SetConsoleCursorPosition(out, pos);
        WriteConsoleA(out, line, (DWORD)len, &written, NULL);
    }
    SetConsoleCursorPosition(out, saved);
}

static WORD agnc_tui_default_attr(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (out != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(out, &csbi)) {
        return csbi.wAttributes;
    }
    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}
#endif
static void agnc_tui_write_clipped_ansi(int row, const char *text)
{
    char line[512];
    size_t length;
    if (text == NULL) {
        text = "";
    }
    snprintf(line, sizeof(line), "%.*s", g_tui_cols - 1, text);
    length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
#ifdef _WIN32
    agnc_tui_paint_row_win32(row, line, agnc_tui_default_attr());
#else
    printf("\033[s");
    agnc_tui_clear_line(row);
    printf("%s%s", AGNC_ANSI_DIM, line);
    printf("%s\033[u", AGNC_ANSI_RESET);
#endif
}
static const char *agnc_tui_active_toast(void)
{
    time_t now = time(NULL);
    if (g_toast[0] == '\0') {
        return NULL;
    }
    if (g_toast_until > 0 && now > g_toast_until) {
        g_toast[0] = '\0';
        g_toast_until = 0;
        return NULL;
    }
    return g_toast;
}
static void agnc_tui_format_status_line(char *line, size_t line_cap)
{
    const char *toast = agnc_tui_active_toast();
    const char *model = g_model[0] != '\0' ? g_model : "?";
    const char *session = g_session[0] != '\0' ? g_session : "?";
    static const char spinner_frames[] = "|/-\\";
    if (line == NULL || line_cap == 0) {
        return;
    }
    line[0] = '\0';
    if (toast != NULL) {
        snprintf(line, line_cap, " %s", toast);
        return;
    }
    if (g_tui_spinner) {
        snprintf(
            line,
            line_cap,
            " %c menunggu respons… | model %s | sesi %s | bg %d+%d",
            spinner_frames[g_tui_spinner_frame % 4],
            model,
            session,
            g_running_jobs,
            g_queue_jobs);
        return;
    }
    if (g_last_turn_tokens >= 0) {
        snprintf(
            line,
            line_cap,
            " model %s | sesi %s | turn %ld tok | bg %d+%d",
            model,
            session,
            g_last_turn_tokens,
            g_running_jobs,
            g_queue_jobs);
    } else {
        snprintf(
            line,
            line_cap,
            " model %s | sesi %s | bg %d+%d",
            model,
            session,
            g_running_jobs,
            g_queue_jobs);
    }
}
#ifdef _WIN32
static void agnc_tui_scroll_chat_region_up(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SMALL_RECT scroll_rect;
    COORD dest_origin;
    CHAR_INFO fill;

    if (out == INVALID_HANDLE_VALUE || g_tui_scroll_bottom < 2) {
        return;
    }
    if (!GetConsoleScreenBufferInfo(out, &csbi)) {
        return;
    }

    scroll_rect.Left = 0;
    scroll_rect.Top = 0;
    scroll_rect.Bottom = (SHORT)(g_tui_scroll_bottom - 1);
    scroll_rect.Right = (SHORT)(g_tui_cols - 1);
    if (scroll_rect.Bottom <= scroll_rect.Top) {
        return;
    }
    dest_origin.X = 0;
    dest_origin.Y = 0;
    fill.Char.UnicodeChar = L' ';
    fill.Attributes = agnc_tui_default_attr();

    ScrollConsoleScreenBuffer(out, &scroll_rect, NULL, dest_origin, &fill);
}
#endif

static void agnc_tui_paint_status_line(void);
static void agnc_tui_render_tools_panel(int start_row);
static void agnc_tui_render_jobs_panel(int start_row);
static void agnc_tui_paint_footer(void);
static void agnc_tui_render_chrome_now(void);

static void agnc_tui_sync_chat_row_from_cursor(void)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SHORT footer_top;

    if (out == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(out, &csbi)) {
        return;
    }
    agnc_tui_compute_layout();
    footer_top = (SHORT)(g_tui_input_row - 1);
    if (csbi.dwCursorPosition.Y >= footer_top) {
        agnc_tui_render_chrome_now();
        return;
    }
    g_tui_chat_row = (int)csbi.dwCursorPosition.Y + 1;
    if (g_tui_chat_row < 1) {
        g_tui_chat_row = 1;
    }
#endif
}

void agnc_tui_chat_write(const char *text)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    size_t len;

    if (!g_tui_active || text == NULL || (!g_tui_chat_emit && g_tui_scroll_lock <= 0)) {
        fputs(text != NULL ? text : "", stdout);
        return;
    }
    len = strlen(text);
    if (len == 0) {
        return;
    }
    agnc_tui_chat_before_write();
    if (out != INVALID_HANDLE_VALUE) {
        WriteConsoleA(out, text, (DWORD)len, &written, NULL);
    }
    agnc_tui_sync_chat_row_from_cursor();
#else
    fputs(text != NULL ? text : "", stdout);
#endif
}

void agnc_tui_chat_write_ln(const char *text)
{
    if (text != NULL && text[0] != '\0') {
        agnc_tui_chat_write(text);
    }
    agnc_tui_chat_newline();
}

void agnc_tui_chat_flush_stdio(void)
{
    if (!g_tui_active || (!g_tui_chat_emit && g_tui_scroll_lock <= 0)) {
        fflush(stdout);
        return;
    }
    fflush(stdout);
    agnc_tui_sync_chat_row_from_cursor();
}

static void agnc_tui_chat_reposition_if_needed(void)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos;

    if (!g_tui_active || (!g_tui_chat_emit && g_tui_scroll_lock <= 0) || out == INVALID_HANDLE_VALUE) {
        return;
    }

    agnc_tui_compute_layout();
    if (g_tui_chat_row < 1) {
        g_tui_chat_row = 1;
    }
    if (g_tui_chat_row > g_tui_scroll_bottom) {
        g_tui_chat_row = g_tui_scroll_bottom;
    }
    pos.X = 0;
    pos.Y = (SHORT)(g_tui_chat_row - 1);
    SetConsoleCursorPosition(out, pos);
#endif
}

int agnc_tui_scroll_locked(void)
{
    return g_tui_active && (g_tui_scroll_lock > 0 || g_tui_chat_emit);
}

void agnc_tui_chat_before_write(void)
{
    agnc_tui_chat_reposition_if_needed();
}

void agnc_tui_chat_newline(void)
{
    if (!g_tui_active || (!g_tui_chat_emit && g_tui_scroll_lock <= 0)) {
        putchar('\n');
        return;
    }

#ifdef _WIN32
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD pos;

        agnc_tui_compute_layout();
        g_tui_chat_row++;
        if (g_tui_chat_row > g_tui_scroll_bottom) {
            agnc_tui_scroll_chat_region_up();
            g_tui_chat_row = g_tui_scroll_bottom;
        }
        pos.X = 0;
        pos.Y = (SHORT)(g_tui_chat_row - 1);
        if (out != INVALID_HANDLE_VALUE) {
            SetConsoleCursorPosition(out, pos);
        }
        return;
    }
#else
    putchar('\n');
    g_tui_chat_row++;
#endif
}

static void agnc_tui_paint_footer(void)
{
    int row;
    int footer_start;

    if (!g_tui_active) {
        return;
    }

    agnc_tui_compute_layout();
    footer_start = g_tui_status_row - agnc_tui_footer_lines() + 1;
#ifdef _WIN32
    {
        WORD attr = agnc_tui_default_attr();
        for (row = footer_start; row <= g_tui_status_row; row++) {
            agnc_tui_clear_row_win32(row, attr);
        }
    }
#else
    for (row = footer_start; row <= g_tui_status_row; row++) {
        agnc_tui_clear_line(row);
    }
#endif

    if (g_tui_view == AGNC_TUI_VIEW_TOOLS) {
        agnc_tui_render_tools_panel(g_tui_panel_start);
    } else if (g_tui_view == AGNC_TUI_VIEW_JOBS) {
        agnc_tui_render_jobs_panel(g_tui_panel_start);
    }
    agnc_tui_paint_status_line();
}

static void agnc_tui_paint_status_line(void)
{
    char line[512];
    agnc_tui_format_status_line(line, sizeof(line));
    if (strcmp(line, g_tui_status_cached) == 0) {
        return;
    }
    snprintf(g_tui_status_cached, sizeof(g_tui_status_cached), "%s", line);
#ifdef _WIN32
    agnc_tui_paint_row_win32(g_tui_status_row, line, agnc_tui_default_attr());
#else
    agnc_tui_write_clipped_ansi(g_tui_status_row, line);
    fflush(stdout);
#endif
}

#ifdef _WIN32
static void agnc_tui_paint_spinner_char(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos;
    DWORD written;
    static const char frames[] = "|/-\\";
    char ch;

    if (out == INVALID_HANDLE_VALUE || !g_tui_spinner) {
        return;
    }
    ch = frames[g_tui_spinner_frame % 4];
    pos.X = 1;
    pos.Y = (SHORT)(g_tui_status_row - 1);
    SetConsoleCursorPosition(out, pos);
    WriteConsoleA(out, &ch, 1, &written, NULL);
}
#endif
static void agnc_tui_render_tools_panel(int start_row)
{
    int row;
    int index;
    int shown = 0;
    for (row = 0; row < AGNC_TUI_PANEL_LINES; row++) {
        index = (int)g_tool_log_count - AGNC_TUI_PANEL_LINES + row;
        if (index >= 0 && (size_t)index < g_tool_log_count) {
            char line[160];
            snprintf(line, sizeof(line), " tool: %s", g_tool_log[index]);
            agnc_tui_write_clipped_ansi(start_row + row, line);
            shown = 1;
        } else {
#ifdef _WIN32
            agnc_tui_clear_row_win32(start_row + row, agnc_tui_default_attr());
#else
            agnc_tui_clear_line(start_row + row);
#endif
        }
    }
    if (!shown) {
        agnc_tui_write_clipped_ansi(start_row, " tool: (belum ada aktivitas — /view off untuk sembunyikan panel)");
    }
}
static void agnc_tui_render_jobs_panel(int start_row)
{
    char line[512];
    int row;
    snprintf(
        line,
        sizeof(line),
        " jobs: running=%d queued=%d  (/jobs detail · /view off)",
        g_running_jobs,
        g_queue_jobs);
    agnc_tui_write_clipped_ansi(start_row, line);
    for (row = 1; row < AGNC_TUI_PANEL_LINES; row++) {
#ifdef _WIN32
        agnc_tui_clear_row_win32(start_row + row, agnc_tui_default_attr());
#else
        agnc_tui_clear_line(start_row + row);
#endif
    }
}
static void agnc_tui_render_chrome_now(void)
{
    if (!g_tui_active) {
        return;
    }
    agnc_tui_apply_scroll_region();
    agnc_tui_paint_footer();
#ifdef _WIN32
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD pos;
        if (out != INVALID_HANDLE_VALUE) {
            pos.X = 0;
            pos.Y = (SHORT)(g_tui_input_row - 1);
            SetConsoleCursorPosition(out, pos);
        }
    }
#else
    agnc_tui_goto_row(g_tui_input_row);
    fflush(stdout);
#endif
    g_tui_chrome_dirty = 0;
}
static void agnc_tui_render_chrome(void)
{
    if (!g_tui_active) {
        return;
    }
    if (g_tui_scroll_lock > 0) {
        g_tui_chrome_dirty = 1;
        return;
    }
    agnc_tui_render_chrome_now();
}
void agnc_tui_init(int enabled)
{
    g_tui_enabled = enabled ? 1 : 0;
    g_tui_active = g_tui_enabled && agnc_console_vt_enabled();
    g_tui_view = AGNC_TUI_VIEW_NORMAL;
    g_tui_scroll_lock = 0;
    g_tui_chat_emit = 0;
    g_tui_chrome_dirty = 0;
    g_tui_chat_row = 1;
    g_tui_status_cached[0] = '\0';
    g_tui_spinner = 0;
    g_tui_spinner_frame = 0;
    g_model[0] = '\0';
    g_session[0] = '\0';
    g_last_turn_tokens = -1;
    g_queue_jobs = 0;
    g_running_jobs = 0;
    g_toast[0] = '\0';
    g_toast_until = 0;
    g_tool_log_count = 0;
    if (g_tui_active) {
        agnc_tui_clear_screen();
    }
}
void agnc_tui_shutdown(void)
{
    g_tui_spinner = 0;
    if (g_tui_active) {
        agnc_tui_set_cursor_visible(1);
        agnc_tui_reset_scroll_region();
    }
    g_tui_active = 0;
    g_tui_scroll_lock = 0;
    g_tui_chat_emit = 0;
    g_tui_chrome_dirty = 0;
}
int agnc_tui_is_active(void)
{
    return g_tui_active;
}
void agnc_tui_clear_screen(void)
{
    if (!g_tui_active) {
        return;
    }
    printf("\033[2J\033[H\033[3J");
    fflush(stdout);
    g_tui_chat_row = 1;
    agnc_tui_apply_scroll_region();
}
void agnc_tui_get_prompt_pos(int *row, int *col)
{
    if (!g_tui_active) {
        if (row != NULL) {
            *row = 0;
        }
        if (col != NULL) {
            *col = 0;
        }
        return;
    }
    agnc_tui_compute_layout();
    if (row != NULL) {
        *row = g_tui_input_row;
    }
    if (col != NULL) {
        *col = 3;
    }
}
void agnc_tui_repaint_status(void)
{
    if (!g_tui_active) {
        return;
    }
    agnc_tui_compute_layout();
    agnc_tui_paint_status_line();
}
void agnc_tui_clear_input_row(void)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos;

    if (!g_tui_active || out == INVALID_HANDLE_VALUE) {
        return;
    }
    agnc_tui_compute_layout();
    agnc_tui_clear_row_win32(g_tui_input_row, agnc_tui_default_attr());
    pos.X = 0;
    pos.Y = (SHORT)(g_tui_input_row - 1);
    SetConsoleCursorPosition(out, pos);
#endif
}

void agnc_tui_begin_scroll_output(void)
{
    if (!g_tui_active) {
        return;
    }
    if (g_tui_scroll_lock == 0) {
        agnc_tui_compute_layout();
        agnc_tui_apply_scroll_region();
        agnc_tui_chat_before_write();
    }
    g_tui_scroll_lock++;
}
void agnc_tui_end_scroll_output(void)
{
    if (!g_tui_active || g_tui_scroll_lock == 0) {
        return;
    }
    g_tui_scroll_lock--;
    if (g_tui_scroll_lock == 0) {
        fflush(stdout);
        agnc_tui_render_chrome_now();
    }
}
void agnc_tui_set_spinner(int active)
{
    g_tui_spinner = active ? 1 : 0;
    if (!active) {
        g_tui_spinner_frame = 0;
        g_tui_status_cached[0] = '\0';
        agnc_tui_set_cursor_visible(1);
        agnc_tui_render_chrome();
        return;
    }
    g_tui_status_cached[0] = '\0';
    agnc_tui_set_cursor_visible(0);
    agnc_tui_spinner_tick();
}
void agnc_tui_spinner_tick(void)
{
    if (!g_tui_active || !g_tui_spinner) {
        return;
    }
    g_tui_spinner_frame = (g_tui_spinner_frame + 1) % 4;
#ifdef _WIN32
    if (g_tui_status_cached[0] != '\0') {
        agnc_tui_paint_spinner_char();
        return;
    }
#endif
    g_tui_status_cached[0] = '\0';
    agnc_tui_compute_layout();
    agnc_tui_paint_status_line();
}
void agnc_tui_set_view(agnc_tui_view_mode_t mode)
{
    if (mode < AGNC_TUI_VIEW_NORMAL || mode > AGNC_TUI_VIEW_JOBS) {
        return;
    }
    g_tui_view = mode;
    agnc_tui_render_chrome();
}
agnc_tui_view_mode_t agnc_tui_get_view(void)
{
    return g_tui_view;
}
void agnc_tui_update_status(const agnc_tui_status_t *status)
{
    if (status == NULL || !g_tui_active) {
        return;
    }
    if (status->model != NULL) {
        snprintf(g_model, sizeof(g_model), "%s", status->model);
    }
    if (status->session != NULL) {
        snprintf(g_session, sizeof(g_session), "%s", status->session);
    }
    g_last_turn_tokens = status->last_turn_tokens;
    g_queue_jobs = status->queue_jobs;
    g_running_jobs = status->running_jobs;
    if (status->toast != NULL && status->toast[0] != '\0') {
        agnc_tui_set_toast(status->toast);
        return;
    }
    if (g_tui_spinner) {
        agnc_tui_paint_status_line();
        return;
    }
    agnc_tui_paint_status_line();
    if (g_tui_view == AGNC_TUI_VIEW_JOBS) {
        agnc_tui_compute_layout();
        agnc_tui_render_jobs_panel(g_tui_panel_start);
    }
}
void agnc_tui_set_toast(const char *message)
{
    if (message == NULL || message[0] == '\0') {
        g_toast[0] = '\0';
        g_toast_until = 0;
        agnc_tui_render_chrome();
        return;
    }
    snprintf(g_toast, sizeof(g_toast), "%s", message);
    g_toast_until = time(NULL) + 12;
    agnc_tui_render_chrome();
}
void agnc_tui_tool_log(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (g_tool_log_count < AGNC_TUI_TOOL_LOG_MAX) {
        snprintf(g_tool_log[g_tool_log_count], AGNC_TUI_TOOL_LINE_MAX, "%s", line);
        g_tool_log_count++;
    } else {
        size_t index;
        for (index = 1; index < AGNC_TUI_TOOL_LOG_MAX; index++) {
            memcpy(g_tool_log[index - 1], g_tool_log[index], AGNC_TUI_TOOL_LINE_MAX);
        }
        snprintf(g_tool_log[AGNC_TUI_TOOL_LOG_MAX - 1], AGNC_TUI_TOOL_LINE_MAX, "%s", line);
    }
    if (g_tui_view == AGNC_TUI_VIEW_TOOLS) {
        agnc_tui_render_chrome();
    }
}
void agnc_tui_before_prompt(void)
{
    if (!g_tui_active) {
        return;
    }
    if (g_tui_scroll_lock > 0) {
        g_tui_chrome_dirty = 1;
        return;
    }
    agnc_tui_render_chrome();
}
void agnc_tui_show_prompt(void)
{
    agnc_tui_before_prompt();
    if (!g_tui_active) {
        printf("> ");
        fflush(stdout);
    }
}
void agnc_tui_begin_chat_output(void)
{
    if (!g_tui_active) {
        return;
    }
    g_tui_chat_emit = 1;
    agnc_tui_compute_layout();
    agnc_tui_chat_before_write();
}

void agnc_tui_end_chat_output(void)
{
    g_tui_chat_emit = 0;
    fflush(stdout);
    if (g_tui_active) {
        agnc_tui_render_chrome_now();
    }
}
