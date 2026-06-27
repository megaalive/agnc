/*
 * console.c
 *
 * Setup konsol UTF-8 + ANSI VT dan wrapper cetak respons.
 * Diadaptasi dari proyek agency.
 */

#include "agnc/console.h"
#include "agnc/markdown_render.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define ANSI_RESET AGNC_ANSI_RESET
#define ANSI_DIM AGNC_ANSI_DIM
#define ANSI_USER AGNC_ANSI_USER

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int g_console_vt_enabled = 0;

static void agnc_console_enable_vt(void)
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    /* Aktifkan ANSI color di Windows Terminal / konsol modern. */
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(out, mode)) {
            g_console_vt_enabled = 1;
        }
    }
#else
    g_console_vt_enabled = 1;
#endif
}

void agnc_console_init(void)
{
#ifdef _WIN32
    /* Cegah mojibake emoji dan karakter UTF-8 dari model. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    agnc_console_enable_vt();
}

int agnc_console_vt_enabled(void)
{
    return g_console_vt_enabled;
}

void agnc_console_format_time_hm(char *out, size_t out_cap)
{
    time_t now;
    struct tm local_tm;

    if (out == NULL || out_cap < 6) {
        return;
    }

    out[0] = '\0';
    time(&now);
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    snprintf(out, out_cap, "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
}

void agnc_console_format_time_hms(char *out, size_t out_cap)
{
    time_t now;
    struct tm local_tm;

    if (out == NULL || out_cap < 9) {
        return;
    }

    out[0] = '\0';
    time(&now);
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    snprintf(out, out_cap, "%02d:%02d:%02d", local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
}

void agnc_console_print_role_header(const char *role)
{
    if (role == NULL) {
        return;
    }

    printf("%s :\n", role);
    fflush(stdout);
}

void agnc_console_print_timestamped_message(const char *text)
{
    char time_buf[8];

    if (text == NULL) {
        return;
    }

    agnc_console_format_time_hm(time_buf, sizeof(time_buf));
    agnc_markdown_render_body(text, time_buf, 8);
}

void agnc_console_print_assistant_body(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    /* Mode --print headless: tanpa prefix waktu/role, hanya body terformat. */
    agnc_markdown_render_body(text, NULL, 0);
    fflush(stdout);
}

void agnc_console_clear_input_line(void)
{
    if (agnc_console_vt_enabled()) {
        /* Kursor setelah fgets ada di baris baru; naik satu baris lalu hapus input mentah. */
        fputs("\033[1A\033[2K\r", stdout);
    } else {
        fputs("\n", stdout);
    }
    fflush(stdout);
}

static void agnc_console_print_chat_timestamp_line(void)
{
    char time_buf[16];

    agnc_console_format_time_hms(time_buf, sizeof(time_buf));
    if (agnc_console_vt_enabled()) {
        printf(ANSI_DIM "%s" ANSI_RESET "\n", time_buf);
    } else {
        printf("%s\n", time_buf);
    }
    fflush(stdout);
}

void agnc_console_print_chat_user(const char *text)
{
    agnc_console_print_chat_timestamp_line();
    if (text == NULL || text[0] == '\0') {
        return;
    }

    if (agnc_console_vt_enabled()) {
        printf(ANSI_USER "%s" ANSI_RESET "\n", text);
    } else {
        printf("%s\n", text);
    }
    fflush(stdout);
}

void agnc_console_print_chat_assistant_begin(void)
{
    agnc_console_print_chat_timestamp_line();
}

void agnc_console_print_chat_system(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    agnc_console_print_chat_timestamp_line();
    if (agnc_console_vt_enabled()) {
        printf(ANSI_DIM "agnc" ANSI_RESET " · %s\n", text);
    } else {
        printf("agnc · %s\n", text);
    }
    fflush(stdout);
}

void agnc_console_print_permission_prompt(const char *label, const char *detail)
{
    agnc_console_spinner_stop();

    if (label == NULL) {
        label = "izinkan?";
    }

    if (agnc_console_vt_enabled()) {
        printf(ANSI_DIM "agnc" ANSI_RESET " · %s\n", label);
        if (detail != NULL && detail[0] != '\0') {
            printf("  %s\n", detail);
        }
        printf("  [y/N] ");
    } else {
        printf("agnc · %s\n", label);
        if (detail != NULL && detail[0] != '\0') {
            printf("%s\n", detail);
        }
        printf("[y/N] ");
    }
    fflush(stdout);
}

#ifdef _WIN32

void agnc_console_input_begin(agnc_console_input_session_t *session)
{
    HANDLE in;

    if (session == NULL) {
        return;
    }

    session->in_handle = NULL;
    session->out_handle = NULL;
    session->saved_mode = 0;
    session->active = 0;

    in = GetStdHandle(STD_INPUT_HANDLE);
    session->out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (in == INVALID_HANDLE_VALUE) {
        return;
    }

    session->in_handle = in;
    if (GetConsoleMode(in, (DWORD *)&session->saved_mode)) {
        DWORD raw = (DWORD)session->saved_mode;

        raw &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        /* Biarkan ENABLE_QUICK_EDIT_MODE: seleksi mouse + paste klik kanan di PowerShell. */
        FlushConsoleInputBuffer(in);
        SetConsoleMode(in, raw);
        session->active = 1;
    }
}

void agnc_console_input_end(agnc_console_input_session_t *session)
{
    HANDLE in;

    if (session == NULL || !session->active) {
        return;
    }

    in = (HANDLE)session->in_handle;
    SetConsoleMode(in, (DWORD)session->saved_mode);
    session->active = 0;
}

void agnc_console_input_prepare_repl(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

    fflush(stdout);
    if (in != INVALID_HANDLE_VALUE) {
        FlushConsoleInputBuffer(in);
    }
}

void agnc_console_input_echo_char(agnc_console_input_session_t *session, char ch)
{
    HANDLE out;
    DWORD written;

    if (session == NULL || session->out_handle == NULL) {
        return;
    }

    out = (HANDLE)session->out_handle;
    WriteConsoleA(out, &ch, 1, &written, NULL);
}

void agnc_console_input_echo_backspace(agnc_console_input_session_t *session)
{
    agnc_console_input_echo_char(session, '\b');
    agnc_console_input_echo_char(session, ' ');
    agnc_console_input_echo_char(session, '\b');
}

void agnc_console_input_echo_newline(agnc_console_input_session_t *session)
{
    agnc_console_input_echo_char(session, '\n');
}

int agnc_console_input_key_printable(unsigned long control_state, unsigned short vk, char ascii_char)
{
    (void)vk;

    if ((control_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0) {
        return ascii_char == 3;
    }
    if ((control_state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0) {
        return 0;
    }

    return ascii_char >= 32 && ascii_char != 127;
}

int agnc_console_input_is_paste_key(unsigned long control_state, unsigned short vk, char ascii_char)
{
    if ((control_state & SHIFT_PRESSED) != 0 && vk == VK_INSERT) {
        return 1;
    }
    if ((control_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0) {
        if (vk == 'V' || vk == 'v' || ascii_char == 0x16) {
            return 1;
        }
    }
    return 0;
}

static void agnc_console_sanitize_paste(char *text)
{
    size_t index;

    if (text == NULL) {
        return;
    }

    for (index = 0; text[index] != '\0'; index++) {
        if (text[index] == '\r' || text[index] == '\n' || text[index] == '\t') {
            text[index] = ' ';
        } else if ((unsigned char)text[index] < 32) {
            text[index] = ' ';
        }
    }
}

size_t agnc_console_input_paste_clipboard(char *dest, size_t capacity)
{
    HGLOBAL memory;
    size_t length = 0;

    if (dest == NULL || capacity == 0) {
        return 0;
    }

    dest[0] = '\0';
    if (!OpenClipboard(NULL)) {
        return 0;
    }

    memory = GetClipboardData(CF_UNICODETEXT);
    if (memory != NULL) {
        const wchar_t *wide = (const wchar_t *)GlobalLock(memory);

        if (wide != NULL) {
            int converted = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dest, (int)capacity, NULL, NULL);

            if (converted > 0) {
                length = (size_t)(converted - 1);
            }
            GlobalUnlock(memory);
        }
    } else {
        memory = GetClipboardData(CF_TEXT);
        if (memory != NULL) {
            const char *text = (const char *)GlobalLock(memory);

            if (text != NULL) {
                snprintf(dest, capacity, "%s", text);
                length = strlen(dest);
                GlobalUnlock(memory);
            }
        }
    }

    CloseClipboard();
    agnc_console_sanitize_paste(dest);
    return length;
}

#endif /* _WIN32 */

void agnc_console_read_yes_no(int *allowed)
{
    char line[32];
    size_t length = 0;

    if (allowed == NULL) {
        return;
    }

    *allowed = 0;
    line[0] = '\0';

#ifdef _WIN32
    {
        agnc_console_input_session_t session;
        INPUT_RECORD record;
        DWORD read_count = 0;

        agnc_console_input_begin(&session);

        for (;;) {
            HANDLE in = (HANDLE)session.in_handle;

            if (in == NULL ||
                !ReadConsoleInputA(in, &record, 1, &read_count) ||
                read_count == 0) {
                break;
            }

            if (record.EventType == MOUSE_EVENT) {
                continue;
            }

            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
                continue;
            }

            if (agnc_console_input_is_paste_key(
                    record.Event.KeyEvent.dwControlKeyState,
                    record.Event.KeyEvent.wVirtualKeyCode,
                    record.Event.KeyEvent.uChar.AsciiChar)) {
                char paste_buf[32];
                size_t pasted = agnc_console_input_paste_clipboard(paste_buf, sizeof(paste_buf));

                if (pasted > 0 && length + pasted < sizeof(line)) {
                    memcpy(line + length, paste_buf, pasted);
                    length += pasted;
                    line[length] = '\0';
                    fwrite(paste_buf, 1, pasted, stdout);
                    fflush(stdout);
                }
                continue;
            }

            if (record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                agnc_console_input_echo_newline(&session);
                break;
            }

            if (record.Event.KeyEvent.wVirtualKeyCode == VK_BACK) {
                if (length > 0) {
                    length--;
                    line[length] = '\0';
                    agnc_console_input_echo_backspace(&session);
                }
                continue;
            }

            if (agnc_console_input_key_printable(
                    record.Event.KeyEvent.dwControlKeyState,
                    record.Event.KeyEvent.wVirtualKeyCode,
                    record.Event.KeyEvent.uChar.AsciiChar) &&
                length + 1 < sizeof(line)) {
                char ch = record.Event.KeyEvent.uChar.AsciiChar;

                line[length++] = ch;
                line[length] = '\0';
                agnc_console_input_echo_char(&session, ch);
            }
        }

        agnc_console_input_end(&session);
        *allowed = (line[0] == 'y' || line[0] == 'Y');
        return;
    }
#else
    if (fgets(line, sizeof(line), stdin) != NULL) {
        *allowed = (line[0] == 'y' || line[0] == 'Y');
    }
#endif
}

void agnc_console_print_chat_tool(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    agnc_console_spinner_stop();
    if (agnc_console_vt_enabled()) {
        printf(ANSI_DIM "agnc" ANSI_RESET " · %s\n", text);
    } else {
        printf("agnc · %s\n", text);
    }
    fflush(stdout);
}

#ifdef _WIN32

static volatile int g_spinner_active = 0;
static HANDLE g_spinner_thread = NULL;

static DWORD WINAPI agnc_console_spinner_thread(LPVOID unused)
{
    static const char *frames[] = {"|", "/", "-", "\\"};
    size_t index = 0;

    (void)unused;

    while (g_spinner_active) {
        if (agnc_console_vt_enabled()) {
            fprintf(stdout, "\r\033[2K" ANSI_DIM "%s menunggu respons…" ANSI_RESET, frames[index % 4]);
        } else {
            fprintf(stdout, "\rmenunggu respons… %c   ", frames[index % 4][0]);
        }
        fflush(stdout);
        index++;
        Sleep(100);
    }

    return 0;
}

void agnc_console_spinner_start(void)
{
    if (g_spinner_active) {
        return;
    }

    g_spinner_active = 1;
    g_spinner_thread = CreateThread(NULL, 0, agnc_console_spinner_thread, NULL, 0, NULL);
}

void agnc_console_spinner_stop(void)
{
    if (!g_spinner_active) {
        return;
    }

    g_spinner_active = 0;
    if (g_spinner_thread != NULL) {
        WaitForSingleObject(g_spinner_thread, 5000);
        CloseHandle(g_spinner_thread);
        g_spinner_thread = NULL;
    }

    if (agnc_console_vt_enabled()) {
        fputs("\r\033[2K\n", stdout);
    } else {
        fputs("\r                    \r\n", stdout);
    }
    fflush(stdout);
}

#else

void agnc_console_spinner_start(void)
{
    if (agnc_console_vt_enabled()) {
        fputs(ANSI_DIM "menunggu respons…" ANSI_RESET "\n", stdout);
    } else {
        fputs("menunggu respons…\n", stdout);
    }
    fflush(stdout);
}

void agnc_console_spinner_stop(void)
{
    if (agnc_console_vt_enabled()) {
        fputs("\033[1A\033[2K\r", stdout);
    } else {
        fputs("\n", stdout);
    }
    fflush(stdout);
}

#endif
