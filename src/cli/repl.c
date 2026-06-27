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
#include "agnc/path.h"
#include "agnc/permissions.h"
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
    printf("  /mcp [reconnect]   Status server MCP; reconnect memuat ulang koneksi\n");
    printf("  /session           Daftar sesi tersimpan\n");
    printf("  /session <nama>    Simpan sesi ini, pindah ke sesi lain\n");
    printf("  /session new <nama>  Sesi baru kosong dengan nama tersebut\n");
    printf("  /session delete <nama>  Hapus file sesi dari disk\n");
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

static size_t agnc_repl_mcp_tool_count_for_server(const agnc_mcp_tool_catalog_t *catalog, size_t server_index)
{
    size_t index;
    size_t count = 0;

    if (catalog == NULL || catalog->tools == NULL) {
        return 0;
    }

    for (index = 0; index < catalog->count; index++) {
        if (catalog->tools[index].server_index == server_index) {
            count++;
        }
    }

    return count;
}

static int agnc_repl_mcp_server_connected(
    const agnc_mcp_session_t *mcp_session,
    const char *server_id,
    size_t *connected_index_out)
{
    size_t index;

    if (connected_index_out != NULL) {
        *connected_index_out = 0;
    }

    if (mcp_session == NULL || !mcp_session->loaded || server_id == NULL) {
        return 0;
    }

    for (index = 0; index < agnc_mcp_registry_server_count(&mcp_session->registry); index++) {
        const agnc_mcp_connected_server_t *server =
            agnc_mcp_registry_server_at(&mcp_session->registry, index);

        if (server != NULL && server->server_id != NULL && strcmp(server->server_id, server_id) == 0 &&
            server->client.initialized) {
            if (connected_index_out != NULL) {
                *connected_index_out = index;
            }
            return 1;
        }
    }

    return 0;
}

static void agnc_repl_print_mcp_status(const agnc_config_t *config, agnc_mcp_session_t *mcp_session)
{
    size_t index;
    size_t enabled_count = 0;
    size_t connected_count = 0;

    if (config->mcp_server_count == 0) {
        agnc_console_print_chat_system("MCP: tidak ada mcp.servers di config");
        return;
    }

    if (!mcp_session->loaded) {
        (void)agnc_mcp_session_ensure(mcp_session, config, 30000);
    }

    connected_count = agnc_mcp_registry_server_count(&mcp_session->registry);
    for (index = 0; index < config->mcp_server_count; index++) {
        if (config->mcp_servers[index].enabled) {
            enabled_count++;
        }
    }

    printf("MCP: %zu server dikonfigurasi, %zu enabled, %zu terhubung, %zu tool diekspos\n",
        config->mcp_server_count,
        enabled_count,
        connected_count,
        mcp_session->catalog.count);

    for (index = 0; index < config->mcp_server_count; index++) {
        const agnc_mcp_server_config_t *entry = &config->mcp_servers[index];
        size_t connected_index = 0;
        int connected = agnc_repl_mcp_server_connected(
            mcp_session,
            entry->id != NULL ? entry->id : "",
            &connected_index);
        size_t tool_count = connected ? agnc_repl_mcp_tool_count_for_server(&mcp_session->catalog, connected_index) : 0;

        printf("  %-20s %-9s %-11s tools=%zu\n",
            entry->id != NULL ? entry->id : "(no-id)",
            entry->enabled ? "enabled" : "disabled",
            connected ? "connected" : (entry->enabled ? "offline" : "—"),
            tool_count);
        if (entry->command != NULL) {
            printf("    command: %s", entry->command);
            if (entry->arg_count > 0 && entry->args[0] != NULL) {
                printf(" %s", entry->args[0]);
            }
            printf("\n");
        }
    }
}

static void agnc_repl_print_usage_summary(long prompt_tokens, long completion_tokens, long total_tokens)
{
    char detail[128];

    if (prompt_tokens < 0 && completion_tokens < 0 && total_tokens < 0) {
        return;
    }

    if (total_tokens >= 0) {
        snprintf(detail, sizeof(detail), "token: total %ld", total_tokens);
    } else if (prompt_tokens >= 0 && completion_tokens >= 0) {
        snprintf(detail, sizeof(detail), "token: prompt %ld · completion %ld", prompt_tokens, completion_tokens);
    } else if (prompt_tokens >= 0) {
        snprintf(detail, sizeof(detail), "token: prompt %ld", prompt_tokens);
    } else if (completion_tokens >= 0) {
        snprintf(detail, sizeof(detail), "token: completion %ld", completion_tokens);
    } else {
        return;
    }

    agnc_console_print_chat_system(detail);
}

static agnc_status_t agnc_repl_reload_config(agnc_config_t *config)
{
    agnc_config_free(config);
    agnc_config_init(config);
    return agnc_config_load(NULL, config);
}

static void agnc_repl_apply_loaded_session_meta(
    agnc_config_t *config,
    char *loaded_provider,
    char *loaded_model)
{
    if (loaded_provider != NULL) {
#ifdef _WIN32
        _putenv_s("AGNC_PROVIDER", loaded_provider);
#else
        setenv("AGNC_PROVIDER", loaded_provider, 1);
#endif
        (void)agnc_repl_reload_config(config);
    }
    if (loaded_model != NULL) {
        free(config->model);
        config->model = loaded_model;
        loaded_model = NULL;
    }
    free(loaded_provider);
    free(loaded_model);
}

static void agnc_repl_notify_memory_window(agnc_conversation_t *conversation)
{
    if (conversation == NULL) {
        return;
    }

    if (conversation->memory_skipped > 0) {
        char detail[96];
        snprintf(
            detail,
            sizeof(detail),
            "memuat %zu pesan terakhir (%zu pesan lebih lama di storage)",
            conversation->count,
            conversation->memory_skipped);
        agnc_console_print_chat_system(detail);
    }
}

static void agnc_repl_print_sessions(const char *active_name)
{
    char *dir = NULL;
    char **names = NULL;
    size_t count = 0;
    size_t index;

    if (agnc_session_default_dir(&dir) != AGNC_STATUS_OK) {
        agnc_console_print_chat_system("session: gagal baca folder sesi");
        return;
    }

    (void)agnc_session_list_names(dir, &names, &count);

    printf("Sesi (aktif: %s):\n", active_name != NULL ? active_name : "current");
    if (count == 0) {
        printf("  (belum ada file sesi)\n");
    } else {
        for (index = 0; index < count; index++) {
            const char *marker = "";

            if (active_name != NULL && names[index] != NULL && strcmp(names[index], active_name) == 0) {
                marker = " *";
            }
            printf("  %s%s\n", names[index], marker);
        }
    }

    agnc_session_list_names_free(names, count);
    free(dir);
}

static agnc_status_t agnc_repl_switch_session(
    const char *name,
    char **session_path,
    char **active_session_name,
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    int force_empty,
    int skip_save)
{
    char *new_path = NULL;
    char *loaded_provider = NULL;
    char *loaded_model = NULL;
    agnc_status_t status;

    status = agnc_session_validate_name(name);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (!skip_save && *session_path != NULL) {
        (void)agnc_session_sync(*session_path, conversation, config);
    }

    status = agnc_session_path_for_name(name, &new_path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    agnc_conversation_clear(conversation);

    if (!force_empty && agnc_path_exists(new_path)) {
        if (agnc_session_load(new_path, conversation, &loaded_provider, &loaded_model) == AGNC_STATUS_OK) {
            agnc_repl_apply_loaded_session_meta(config, loaded_provider, loaded_model);
            loaded_provider = NULL;
            loaded_model = NULL;
            agnc_repl_notify_memory_window(conversation);
        }
    } else {
        (void)agnc_session_sync(new_path, conversation, config);
    }

    free(*session_path);
    *session_path = new_path;

    free(*active_session_name);
    *active_session_name = agnc_strdup_local(name);
    if (*active_session_name == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return agnc_session_active_name_save(name);
}

static agnc_status_t agnc_repl_delete_session(
    const char *name,
    char **session_path,
    char **active_session_name,
    agnc_config_t *config,
    agnc_conversation_t *conversation)
{
    char *path = NULL;
    int is_active;
    agnc_status_t status;

    status = agnc_session_validate_name(name);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    status = agnc_session_path_for_name(name, &path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }
    free(path);

    is_active = *active_session_name != NULL && strcmp(*active_session_name, name) == 0;
    if (is_active) {
        status = agnc_repl_switch_session(
            "current", session_path, active_session_name, config, conversation, 1, 1);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    } else if (*session_path != NULL) {
        (void)agnc_session_sync(*session_path, conversation, config);
    }

    return agnc_session_delete_by_name(name);
}

static int agnc_repl_handle_slash(
    char *line,
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    char **session_path,
    char **active_session_name,
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
            (void)agnc_session_clear_messages(*session_path, config);
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
        } else if (*session_path != NULL) {
            status = agnc_session_compact_storage(*session_path, conversation, config, keep);
            if (status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("compact storage gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(status));
            } else {
                char detail[64];
                snprintf(detail, sizeof(detail), "riwayat diringkas (keep %zu pesan)", keep);
                agnc_console_print_chat_system(detail);
            }
        } else {
            char detail[64];
            snprintf(detail, sizeof(detail), "riwayat diringkas (keep %zu pesan)", keep);
            agnc_console_print_chat_system(detail);
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

    if (strncmp(line, "/mcp", 4) == 0) {
        arg = line + 4;
        while (*arg == ' ') {
            arg++;
        }

        if (strncmp(arg, "reconnect", 9) == 0) {
            agnc_status_t reconnect_status = agnc_mcp_session_reconnect(mcp_session, config, 30000);

            if (reconnect_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("MCP reconnect gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(reconnect_status));
            } else {
                char detail[64];
                snprintf(
                    detail,
                    sizeof(detail),
                    "MCP reconnect OK (%zu server, %zu tool)",
                    agnc_mcp_registry_server_count(&mcp_session->registry),
                    mcp_session->catalog.count);
                agnc_console_print_chat_system(detail);
            }
            return 1;
        }

        agnc_console_print_chat_system("MCP");
        agnc_repl_print_mcp_status(config, mcp_session);
        return 1;
    }

    if (strncmp(line, "/session", 8) == 0) {
        arg = line + 8;
        while (*arg == ' ') {
            arg++;
        }

        if (*arg == '\0') {
            agnc_repl_print_sessions(*active_session_name);
            return 1;
        }

        if (strncmp(arg, "delete ", 7) == 0) {
            const char *delete_name = arg + 7;
            agnc_status_t delete_status;

            while (*delete_name == ' ') {
                delete_name++;
            }
            if (*delete_name == '\0') {
                agnc_console_print_chat_system("session: nama wajib (/session delete <nama>)");
                return 1;
            }

            delete_status = agnc_repl_delete_session(
                delete_name, session_path, active_session_name, config, conversation);
            if (delete_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("session delete gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(delete_status));
            } else {
                char detail[80];
                snprintf(detail, sizeof(detail), "sesi dihapus: %s", delete_name);
                agnc_console_print_chat_system(detail);
            }
            return 1;
        }

        if (strncmp(arg, "new ", 4) == 0) {
            const char *new_name = arg + 4;
            agnc_status_t switch_status;

            while (*new_name == ' ') {
                new_name++;
            }
            if (*new_name == '\0') {
                agnc_console_print_chat_system("session: nama wajib (/session new <nama>)");
                return 1;
            }

            switch_status = agnc_repl_switch_session(
                new_name, session_path, active_session_name, config, conversation, 1, 0);
            if (switch_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("session new gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(switch_status));
            } else {
                char detail[80];
                snprintf(detail, sizeof(detail), "sesi baru: %s", new_name);
                agnc_console_print_chat_system(detail);
            }
            return 1;
        }

        {
            agnc_status_t switch_status = agnc_repl_switch_session(
                arg, session_path, active_session_name, config, conversation, 0, 0);

            if (switch_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("session gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(switch_status));
            } else {
                char detail[80];
                snprintf(detail, sizeof(detail), "sesi aktif: %s", arg);
                agnc_console_print_chat_system(detail);
            }
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
    char *active_session_name = NULL;
    char line[AGNC_REPL_LINE_MAX];
    long usage_prompt = -1;
    long usage_completion = -1;
    long usage_total = -1;
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
    agnc_permission_session_reset();
    status = agnc_session_active_name_load(&active_session_name);
    if (status != AGNC_STATUS_OK || active_session_name == NULL) {
        free(active_session_name);
        active_session_name = agnc_strdup_local("current");
    }
    status = agnc_session_path_for_name(active_session_name, &session_path);
    if (status == AGNC_STATUS_OK && session_path != NULL) {
        /* Sisa atomic write dari proses sebelumnya (*.json.tmp.*). */
        (void)agnc_session_cleanup_stale_temp_files();

        char *loaded_provider = NULL;
        char *loaded_model = NULL;

        if (agnc_session_load(session_path, &conversation, &loaded_provider, &loaded_model) == AGNC_STATUS_OK) {
            agnc_repl_apply_loaded_session_meta(&config, loaded_provider, loaded_model);
            agnc_repl_notify_memory_window(&conversation);
        }
    }

    agnc_repl_install_cancel_handler();
    printf(">\n");
    fflush(stdout);

    memset(&options, 0, sizeof(options));
    options.cancel_flag = &g_repl_cancel_flag;
    options.stream_live_print = 0;
    options.chat_assistant_timestamp = 1;
    options.mcp_session = &mcp_session;
    options.usage_prompt_tokens = &usage_prompt;
    options.usage_completion_tokens = &usage_completion;
    options.usage_total_tokens = &usage_total;

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
                    line, &config, &conversation, &session_path, &active_session_name, &mcp_session);
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

        if (status == AGNC_STATUS_OK) {
            agnc_repl_print_usage_summary(usage_prompt, usage_completion, usage_total);
        }

        if (session_path != NULL) {
            (void)agnc_session_sync(session_path, &conversation, &config);
        }

        printf(">\n");
        fflush(stdout);
    }

    if (session_path != NULL) {
        (void)agnc_session_sync(session_path, &conversation, &config);
    }

    free(session_path);
    free(active_session_name);
    agnc_mcp_session_free(&mcp_session);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    return exit_code;
}
