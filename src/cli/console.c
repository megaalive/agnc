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
