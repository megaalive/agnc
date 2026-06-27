/*
 * repl.c
 *
 * Mode interaktif agnc: REPL, slash commands, streaming, session persistence.
 */

#include "agnc/cli.h"
#include "agnc/config.h"
#include "agnc/console.h"
#include "agnc/conversation.h"
#include "agnc/line_edit.h"
#include "agnc/mcp/session.h"
#include "agnc/provider.h"
#include "agnc/query.h"
#include "agnc/session.h"

#include "agnc_integrations_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#endif

#define AGNC_REPL_LINE_MAX 8192
#define AGNC_COMPACT_KEEP_TAIL 12

static volatile int g_repl_cancel_flag = 0;
static volatile int g_repl_in_request = 0;

#ifdef _WIN32
static BOOL WINAPI agnc_repl_ctrl_handler(DWORD event_type)
{
    if (event_type == CTRL_C_EVENT && g_repl_in_request) {
        g_repl_cancel_flag = 1;
        return TRUE;
    }
    return FALSE;
}
#else
static void agnc_repl_sigint_handler(int signum)
{
    (void)signum;
    if (g_repl_in_request) {
        g_repl_cancel_flag = 1;
    }
}
#endif

static void agnc_repl_install_cancel_handler(void)
{
#ifdef _WIN32
    SetConsoleCtrlHandler(agnc_repl_ctrl_handler, TRUE);
#else
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = agnc_repl_sigint_handler;
    sigaction(SIGINT, &action, NULL);
#endif
}

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static void agnc_repl_trim(char *line)
{
    char *start;
    char *end;

    if (line == NULL) {
        return;
    }

    start = line;
    while (*start == ' ' || *start == '\t') {
        start++;
    }

    if (start != line) {
        memmove(line, start, strlen(start) + 1);
    }

    end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
}

static void agnc_repl_print_help(void)
{
    agnc_console_print_chat_system("mode interaktif");
    printf("Slash commands:\n");
    printf("  /help              Tampilkan bantuan ini\n");
    printf("  /clear             Hapus riwayat percakapan\n");
    printf("  /compact [n]       Ringkas riwayat (default keep %d pesan)\n", AGNC_COMPACT_KEEP_TAIL);
    printf("  /model [nama]      Tampilkan atau ganti model aktif\n");
    printf("  /provider [id]     Tampilkan atau ganti provider (env AGNC_PROVIDER)\n");
    printf("  /doctor            Jalankan health check\n");
    printf("  /exit, /quit       Keluar\n");
    printf("\nCtrl+C saat request berjalan membatalkan tanpa keluar REPL.\n");
    printf("  user      HH:MM:SS + teks (hijau)\n");
    printf("  asisten   HH:MM:SS + jawaban (default; nama file abu-abu)\n");
    printf("  agnc      timestamp + pesan sistem (abu-abu)\n");
}

static void agnc_repl_print_provider_status(const agnc_config_t *config)
{
    size_t index;
    size_t count = agnc_registry_gateway_count();

    printf("  aktif:    %s\n", config->provider_id != NULL ? config->provider_id : "?");
    printf("  gateway:  %s\n", config->gateway_id != NULL ? config->gateway_id : "?");
    printf("  model:    %s\n", config->model != NULL ? config->model : "?");
    printf("  base_url: %s\n", config->base_url != NULL ? config->base_url : "?");
    printf("  gateway terdaftar:\n");

    for (index = 0; index < count; index++) {
        const agnc_gateway_descriptor_t *gateway = agnc_registry_gateway_at(index);
        const char *marker = "";

        if (gateway == NULL) {
            continue;
        }
        if (config->gateway_id != NULL && strcmp(config->gateway_id, gateway->id) == 0) {
            marker = " *";
        }
        printf("  %-28s %s%s\n", gateway->id, gateway->label != NULL ? gateway->label : "", marker);
    }
}

static agnc_status_t agnc_repl_reload_config(agnc_config_t *config)
{
    agnc_config_free(config);
    agnc_config_init(config);
    return agnc_config_load(NULL, config);
}

static int agnc_repl_handle_slash(
    char *line,
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    char **session_path,
    agnc_mcp_session_t *mcp_session)
{
    const char *arg;
    char *space;

    if (strncmp(line, "/help", 5) == 0) {
        agnc_repl_print_help();
        return 1;
    }

    if (strncmp(line, "/clear", 6) == 0) {
        agnc_conversation_clear(conversation);
        if (*session_path != NULL) {
            (void)agnc_session_save(*session_path, conversation, config);
        }
        agnc_console_print_chat_system("riwayat dihapus");
        return 1;
    }

    if (strncmp(line, "/compact", 8) == 0) {
        size_t keep = AGNC_COMPACT_KEEP_TAIL;
        agnc_status_t status;

        arg = line + 8;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg != '\0') {
            keep = (size_t)strtoul(arg, NULL, 10);
            if (keep == 0) {
                keep = AGNC_COMPACT_KEEP_TAIL;
            }
        }

        status = agnc_conversation_compact(conversation, keep);
        if (status != AGNC_STATUS_OK) {
            agnc_console_print_chat_system("compact gagal");
            fprintf(stderr, "agnc: %s\n", agnc_status_to_string(status));
        } else {
            char detail[64];
            snprintf(detail, sizeof(detail), "riwayat diringkas (keep %zu pesan)", keep);
            agnc_console_print_chat_system(detail);
            if (*session_path != NULL) {
                (void)agnc_session_save(*session_path, conversation, config);
            }
        }
        return 1;
    }

    if (strncmp(line, "/model", 6) == 0) {
        arg = line + 6;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg == '\0') {
            char detail[256];
            snprintf(detail, sizeof(detail), "model aktif: %s", config->model != NULL ? config->model : "?");
            agnc_console_print_chat_system(detail);
            return 1;
        }

        free(config->model);
        config->model = agnc_strdup_local(arg);
        if (config->model == NULL) {
            agnc_console_print_chat_system("out of memory");
            return 1;
        }
        {
            char detail[256];
            snprintf(detail, sizeof(detail), "model diganti ke %s", config->model);
            agnc_console_print_chat_system(detail);
        }
        return 1;
    }

    if (strncmp(line, "/provider", 9) == 0) {
        arg = line + 9;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg == '\0') {
            agnc_console_print_chat_system("provider");
            agnc_repl_print_provider_status(config);
            return 1;
        }

#ifdef _WIN32
        _putenv_s("AGNC_PROVIDER", arg);
#else
        setenv("AGNC_PROVIDER", arg, 1);
#endif
        if (agnc_repl_reload_config(config) != AGNC_STATUS_OK) {
            agnc_console_print_chat_system("gagal memuat provider (cek ~/.agnc.json)");
        } else {
            agnc_mcp_session_reset(mcp_session);
            char detail[256];
            snprintf(
                detail,
                sizeof(detail),
                "provider %s · model %s",
                config->provider_id != NULL ? config->provider_id : "?",
                config->model != NULL ? config->model : "?");
            agnc_console_print_chat_system(detail);
        }
        return 1;
    }

    if (strncmp(line, "/doctor", 7) == 0) {
        agnc_console_print_chat_system("doctor");
        (void)agnc_cli_run_doctor();
        return 1;
    }

    if (strncmp(line, "/exit", 5) == 0 || strncmp(line, "/quit", 5) == 0) {
        return 2;
    }

    space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
    }
    agnc_console_print_chat_system("perintah tidak dikenal (ketik /help)");
    fprintf(stderr, "agnc: unknown slash command: %s\n", line);
    return 1;
}

int agnc_cli_run_interactive(void)
{
    agnc_config_t config;
    agnc_conversation_t conversation;
    agnc_mcp_session_t mcp_session;
    agnc_query_options_t options;
    char *session_path = NULL;
    char line[AGNC_REPL_LINE_MAX];
    agnc_status_t status;
    int exit_code = 0;

    agnc_config_init(&config);
    status = agnc_config_load(NULL, &config);
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: gagal memuat config (~/.agnc.json): %s\n", agnc_status_to_string(status));
        fprintf(stderr, "agnc: salin config/agnc.example.json ke %%USERPROFILE%%\\.agnc.json\n");
        agnc_config_free(&config);
        return 1;
    }

    if (config.stream == 0) {
        config.stream = 1;
    }

    agnc_conversation_init(&conversation);
    agnc_mcp_session_init(&mcp_session);
    status = agnc_session_current_path(&session_path);
    if (status == AGNC_STATUS_OK && session_path != NULL) {
        /* Sisa atomic write dari proses sebelumnya (current.json.tmp.*). */
        (void)agnc_session_cleanup_stale_temp_files();

        char *loaded_provider = NULL;
        char *loaded_model = NULL;

        if (agnc_session_load(session_path, &conversation, &loaded_provider, &loaded_model) == AGNC_STATUS_OK) {
            if (loaded_provider != NULL) {
#ifdef _WIN32
                _putenv_s("AGNC_PROVIDER", loaded_provider);
#else
                setenv("AGNC_PROVIDER", loaded_provider, 1);
#endif
                (void)agnc_repl_reload_config(&config);
            }
            if (loaded_model != NULL) {
                free(config.model);
                config.model = loaded_model;
                loaded_model = NULL;
            }
            free(loaded_provider);
            free(loaded_model);

            {
                size_t before = conversation.count;

                (void)agnc_conversation_compact_if_needed(
                    &conversation, AGNC_CONVERSATION_COMPACT_THRESHOLD, AGNC_CONVERSATION_COMPACT_KEEP);
                if (conversation.count < before) {
                    agnc_console_print_chat_system("riwayat lama diringkas otomatis");
                }
            }
        }
    }

    agnc_repl_install_cancel_handler();
    agnc_repl_print_help();
    printf(">\n");
    fflush(stdout);

    memset(&options, 0, sizeof(options));
    options.cancel_flag = &g_repl_cancel_flag;
    options.stream_live_print = 0;
    options.chat_assistant_timestamp = 1;
    options.mcp_session = &mcp_session;

    for (;;) {
        if (!agnc_repl_read_line(line, sizeof(line))) {
            break;
        }

        agnc_repl_trim(line);
        if (line[0] == '\0') {
            continue;
        }

        if (line[0] == '/') {
            agnc_console_clear_input_line();
            {
                int slash_result = agnc_repl_handle_slash(
                    line, &config, &conversation, &session_path, &mcp_session);
                if (slash_result == 2) {
                    break;
                }
            }
            printf(">\n");
            fflush(stdout);
            continue;
        }

        agnc_console_clear_input_line();
        agnc_console_print_chat_user(line);

        g_repl_cancel_flag = 0;
        g_repl_in_request = 1;

        status = agnc_query_run(&config, &conversation, line, &options);

        g_repl_in_request = 0;
        g_repl_cancel_flag = 0;

        if (status == AGNC_STATUS_CANCELLED) {
            agnc_console_print_chat_system("request dibatalkan");
            printf(">\n");
            fflush(stdout);
            continue;
        }

        if (session_path != NULL) {
            (void)agnc_session_save(session_path, &conversation, &config);
        }

        printf(">\n");
        fflush(stdout);
    }

    if (session_path != NULL) {
        (void)agnc_session_save(session_path, &conversation, &config);
    }

    free(session_path);
    agnc_mcp_session_free(&mcp_session);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    return exit_code;
}
