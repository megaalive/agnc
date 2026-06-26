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
