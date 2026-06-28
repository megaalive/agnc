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
#include "agnc/opencode.h"
#include "agnc/path.h"
#include "agnc/permissions.h"
#include "agnc/provider.h"
#include "agnc/query.h"
#include "agnc/repl_jobs.h"
#include "agnc/tui.h"
#include "agnc/cost.h"
#include "agnc/session.h"
#include "agnc/hooks.h"
#include "agnc/skills.h"
#include "agnc/tool.h"
#include "agnc/tool_cache.h"

#include "agnc_integrations_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    volatile int *bg_cancel;

    if (event_type == CTRL_C_EVENT) {
        if (g_repl_in_request) {
            g_repl_cancel_flag = 1;
            return TRUE;
        }
        bg_cancel = agnc_repl_jobs_running_cancel_flag();
        if (bg_cancel != NULL) {
            *bg_cancel = 1;
            return TRUE;
        }
        agnc_repl_line_signal_exit();
        return TRUE;
    }
    return FALSE;
}
#else
static void agnc_repl_sigint_handler(int signum)
{
    volatile int *bg_cancel;

    (void)signum;
    if (g_repl_in_request) {
        g_repl_cancel_flag = 1;
        return;
    }
    bg_cancel = agnc_repl_jobs_running_cancel_flag();
    if (bg_cancel != NULL) {
        *bg_cancel = 1;
        return;
    }
    agnc_repl_line_signal_exit();
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

static agnc_conversation_t *g_repl_bg_merge_conversation;
static const char *g_repl_bg_merge_session_path;

static void agnc_repl_bg_merge_bind(agnc_conversation_t *conversation, const char *session_path)
{
    g_repl_bg_merge_conversation = conversation;
    g_repl_bg_merge_session_path = session_path;
}

static int agnc_repl_bg_idle_poll(void)
{
    return agnc_repl_jobs_poll(g_repl_bg_merge_conversation, g_repl_bg_merge_session_path);
}

static void agnc_repl_bg_idle_perm_handle(void)
{
    agnc_repl_jobs_handle_pending_permission();
}

static int agnc_repl_bg_idle_perm_needed(void)
{
    return agnc_repl_jobs_has_pending_permission();
}

static int agnc_repl_bg_idle_needed(void)
{
    return agnc_repl_jobs_wants_idle_poll();
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

static void agnc_repl_refresh_tui_status(
    const agnc_config_t *config,
    const char *session_name,
    long last_turn_tokens)
{
    static char last_model[128];
    static char last_session[64];
    static long last_tokens = -2;
    static int last_queue = -1;
    static int last_running = -1;
    agnc_tui_status_t status;
    int queue_jobs;
    int running_jobs;
    const char *model;
    const char *session;

    if (!agnc_tui_is_active()) {
        return;
    }

    model = config != NULL && config->model != NULL ? config->model : "?";
    session = session_name != NULL ? session_name : "?";
    queue_jobs = agnc_repl_jobs_queue_length();
    running_jobs = agnc_repl_jobs_has_running();

    if (strcmp(model, last_model) == 0 &&
        strcmp(session, last_session) == 0 &&
        last_turn_tokens == last_tokens &&
        queue_jobs == last_queue &&
        running_jobs == last_running) {
        return;
    }

    snprintf(last_model, sizeof(last_model), "%s", model);
    snprintf(last_session, sizeof(last_session), "%s", session);
    last_tokens = last_turn_tokens;
    last_queue = queue_jobs;
    last_running = running_jobs;

    memset(&status, 0, sizeof(status));
    status.model = model;
    status.session = session;
    status.last_turn_tokens = last_turn_tokens;
    status.queue_jobs = queue_jobs;
    status.running_jobs = running_jobs;
    agnc_tui_update_status(&status);
}

static void agnc_repl_print_help(void)
{
    agnc_console_begin_repl_output();
    agnc_console_print_chat_system("mode interaktif");
    agnc_console_repl_printf("Slash commands:\n");
    agnc_console_repl_printf("  /help              Tampilkan bantuan ini\n");
    agnc_console_repl_printf("  /clear             Hapus riwayat percakapan\n");
    agnc_console_repl_printf("  /cls               Bersihkan layar terminal (bukan riwayat chat)\n");
    agnc_console_repl_printf("  /compact [n]       Ringkas riwayat (default keep %d pesan)\n", AGNC_COMPACT_KEEP_TAIL);
    agnc_console_repl_printf("  /models [provider] [filter]  Discovery model semua provider (filter substring)\n");
    agnc_console_repl_printf("  /models --filter PATTERN     Filter nama model (case-insensitive)\n");
    agnc_console_repl_printf("  /model [nama]      Model aktif; tanpa arg = ringkasan, dengan arg = ganti model\n");
    agnc_console_repl_printf("  /provider [id]     Tampilkan atau ganti provider (env AGNC_PROVIDER)\n");
    agnc_console_repl_printf("  /mcp [reconnect]   Status server MCP; reconnect memuat ulang koneksi\n");
    agnc_console_repl_printf("  /session           Daftar sesi tersimpan\n");
    agnc_console_repl_printf("  /session <nama>    Simpan sesi ini, pindah ke sesi lain\n");
    agnc_console_repl_printf("  /session new <nama>  Sesi baru kosong dengan nama tersebut\n");
    agnc_console_repl_printf("  /session delete <nama>  Hapus file sesi dari disk\n");
    agnc_console_repl_printf("  /doctor            Jalankan health check\n");
    agnc_console_repl_printf("  /skills [reload]   Daftar skills aktif; reload muat ulang dari disk\n");
    agnc_console_repl_printf("  /hooks             Daftar hook per event (config hooks.*)\n");
    agnc_console_repl_printf("  /usage             Token usage sesi + turn terakhir\n");
    agnc_console_repl_printf("  /verbose [on|off|toggle]  Log diagnostik runtime.verbose (simpan ke config)\n");
    agnc_console_repl_printf("  /cost              Estimasi biaya USD sesi (heuristik)\n");
    agnc_console_repl_printf("  /bg <prompt>       Jalankan prompt di background (antre hingga %d; sesi aktif)\n", AGNC_REPL_JOB_QUEUE_MAX);
    agnc_console_repl_printf("  & <prompt>         Alias /bg (prefix & di awal baris)\n");
    agnc_console_repl_printf("  /jobs              Status job background; /jobs cancel | clear\n");
    agnc_console_repl_printf("  /view [tools|jobs|off]  Panel TUI bawah (VT terminal; runtime.tui)\n");
    agnc_console_repl_printf("  /exit, /quit       Keluar\n");
    agnc_console_repl_printf("\nWorkspace dan config agnc:\n");
    agnc_console_repl_printf("  Tool workspace = repo root (cwd) atau env AGNC_WORKSPACE.\n");
    agnc_console_repl_printf("  Pindah workspace: set AGNC_WORKSPACE + restart agnc, atau cd ke repo lain lalu jalankan agnc.\n");
    agnc_console_repl_printf("  Config global = ~/.agnc.json (di luar repo); sesi = ~/.agnc/sessions/*.sqlite.\n");
    agnc_console_repl_printf("  Root MCP filesystem = mcp.servers[].args di config; setelah edit config: /mcp reconnect.\n");
    agnc_console_repl_printf("\nCtrl+C           Keluar REPL (layar dibersihkan)\n");
    agnc_console_repl_printf("Ctrl+C saat request / bg job  Membatalkan tanpa keluar\n");
    agnc_console_repl_printf("Ctrl+Enter         Baris baru di prompt (multi-line)\n");
    agnc_console_repl_printf("  user      HH:MM:SS + teks (hijau)\n");
    agnc_console_repl_printf("  asisten   HH:MM:SS + jawaban (default; nama file abu-abu)\n");
    agnc_console_repl_printf("  agnc      timestamp + pesan sistem (abu-abu)\n");
    agnc_console_end_repl_output();
}

static void agnc_repl_print_provider_status(const agnc_config_t *config)
{
    size_t index;
    size_t count = agnc_registry_gateway_count();

    agnc_console_repl_printf("  aktif:    %s\n", config->provider_id != NULL ? config->provider_id : "?");
    agnc_console_repl_printf("  gateway:  %s\n", config->gateway_id != NULL ? config->gateway_id : "?");
    agnc_console_repl_printf("  model:    %s\n", config->model != NULL ? config->model : "?");
    agnc_console_repl_printf("  base_url: %s\n", config->base_url != NULL ? config->base_url : "?");
    agnc_console_repl_printf("  gateway terdaftar:\n");

    for (index = 0; index < count; index++) {
        const agnc_gateway_descriptor_t *gateway = agnc_registry_gateway_at(index);
        const char *marker = "";

        if (gateway == NULL) {
            continue;
        }
        if (config->gateway_id != NULL && strcmp(config->gateway_id, gateway->id) == 0) {
            marker = " *";
        }
        agnc_console_repl_printf("  %-28s %s%s\n", gateway->id, gateway->label != NULL ? gateway->label : "", marker);
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

static void agnc_repl_print_usage_summary(
    const agnc_session_usage_t *session_usage,
    long prompt_tokens,
    long completion_tokens,
    long total_tokens)
{
    char detail[160];

    if (prompt_tokens < 0 && completion_tokens < 0 && total_tokens < 0) {
        return;
    }

    if (session_usage != NULL && session_usage->total_tokens > 0) {
        if (total_tokens >= 0) {
            snprintf(
                detail,
                sizeof(detail),
                "token: turn %ld · sesi %ld",
                total_tokens,
                session_usage->total_tokens);
        } else if (prompt_tokens >= 0 && completion_tokens >= 0) {
            snprintf(
                detail,
                sizeof(detail),
                "token: turn prompt %ld · completion %ld · sesi %ld",
                prompt_tokens,
                completion_tokens,
                session_usage->total_tokens);
        } else {
            snprintf(detail, sizeof(detail), "token: sesi %ld", session_usage->total_tokens);
        }
    } else if (total_tokens >= 0) {
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

static void agnc_repl_print_usage_detail(
    const char *active_session_name,
    const agnc_session_usage_t *session_usage,
    long last_prompt,
    long last_completion,
    long last_total)
{
    const char *name = active_session_name != NULL ? active_session_name : "?";

    agnc_console_repl_printf("Token usage — sesi \"%s\":\n", name);
    if (session_usage != NULL) {
        agnc_console_repl_printf(
            "  sesi: prompt %ld · completion %ld · total %ld\n",
            session_usage->prompt_tokens,
            session_usage->completion_tokens,
            session_usage->total_tokens);
    }

    if (last_prompt < 0 && last_completion < 0 && last_total < 0) {
        agnc_console_repl_printf("  turn terakhir: (belum ada data provider)\n");
        return;
    }

    if (last_total >= 0) {
        agnc_console_repl_printf("  turn terakhir: total %ld\n", last_total);
    } else {
        agnc_console_repl_printf(
            "  turn terakhir: prompt %ld · completion %ld\n",
            last_prompt >= 0 ? last_prompt : 0,
            last_completion >= 0 ? last_completion : 0);
    }
}

static void agnc_repl_usage_reload(agnc_session_usage_t *session_usage, const char *session_path)
{
    if (session_usage == NULL) {
        return;
    }

    if (session_path != NULL) {
        (void)agnc_session_usage_load(session_path, session_usage);
    } else {
        agnc_session_usage_init(session_usage);
    }
}

static agnc_status_t agnc_repl_reload_config(agnc_config_t *config)
{
    agnc_config_t fresh;
    agnc_status_t status;

    if (config == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_config_init(&fresh);
    status = agnc_config_load(NULL, &fresh);
    if (status != AGNC_STATUS_OK) {
        agnc_config_free(&fresh);
        return status;
    }

    agnc_config_free(config);
    *config = fresh;
    return AGNC_STATUS_OK;
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
    agnc_session_usage_t *session_usage,
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

    agnc_repl_usage_reload(session_usage, new_path);
    return agnc_session_active_name_save(name);
}

static agnc_status_t agnc_repl_delete_session(
    const char *name,
    char **session_path,
    char **active_session_name,
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    agnc_session_usage_t *session_usage)
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
            "current", session_path, active_session_name, config, conversation, session_usage, 1, 1);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    } else if (*session_path != NULL) {
        (void)agnc_session_sync(*session_path, conversation, config);
    }

    return agnc_session_delete_by_name(name);
}

static void agnc_repl_print_skills(const agnc_config_t *config, int reloaded)
{
    agnc_skill_entry_t *entries = NULL;
    size_t count = 0;
    size_t index;

    if (config == NULL || !config->skills_enabled) {
        agnc_console_print_chat_system("skills: nonaktif di config");
        return;
    }

    if (agnc_skills_list(config, &entries, &count) != AGNC_STATUS_OK) {
        agnc_console_print_chat_system("skills: gagal baca folder");
        return;
    }

    if (reloaded) {
        agnc_console_print_chat_system("skills: cache dimuat ulang");
    }

    if (count == 0) {
        agnc_console_print_chat_system("skills: tidak ada file (.md atau */SKILL.md)");
        return;
    }

    printf("Skills (%zu):\n", count);
    for (index = 0; index < count; index++) {
        printf("  %s  (%zu bytes)\n  %s\n", entries[index].name, entries[index].size_bytes, entries[index].path);
    }

    agnc_skills_list_free(entries, count);
}

static void agnc_repl_print_hooks(const agnc_config_t *config)
{
    size_t event_index;

    if (config == NULL || !config->hooks_enabled) {
        agnc_console_print_chat_system("hooks: nonaktif di config");
        return;
    }

    printf("Hooks (env: AGNC_HOOK_EVENT, AGNC_HOOK_PAYLOAD_FILE):\n");
    for (event_index = 0; event_index < (size_t)AGNC_HOOK_EVENT_COUNT; event_index++) {
        size_t count = agnc_hooks_count_for_event(config, (agnc_hook_event_id_t)event_index);
        size_t cmd_index;

        printf("  %s (%zu):\n", agnc_hooks_event_name((agnc_hook_event_id_t)event_index), count);
        if (event_index == AGNC_HOOK_EVENT_SESSION_START) {
            for (cmd_index = 0; cmd_index < config->hooks_session_start_count; cmd_index++) {
                printf("    %s\n", config->hooks_session_start[cmd_index]);
            }
        } else if (event_index == AGNC_HOOK_EVENT_PRE_TURN) {
            for (cmd_index = 0; cmd_index < config->hooks_pre_turn_count; cmd_index++) {
                printf("    %s\n", config->hooks_pre_turn[cmd_index]);
            }
        } else if (event_index == AGNC_HOOK_EVENT_POST_TURN) {
            for (cmd_index = 0; cmd_index < config->hooks_post_turn_count; cmd_index++) {
                printf("    %s\n", config->hooks_post_turn[cmd_index]);
            }
        } else if (event_index == AGNC_HOOK_EVENT_PRE_TOOL) {
            for (cmd_index = 0; cmd_index < config->hooks_pre_tool_count; cmd_index++) {
                printf("    %s\n", config->hooks_pre_tool[cmd_index]);
            }
        } else if (event_index == AGNC_HOOK_EVENT_POST_TOOL) {
            for (cmd_index = 0; cmd_index < config->hooks_post_tool_count; cmd_index++) {
                printf("    %s\n", config->hooks_post_tool[cmd_index]);
            }
        }
    }
}

static int agnc_repl_str_ieq(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

/*
 * Parse on|off|true|false|toggle untuk /verbose.
 * Return 0 = tanpa arg (tampilkan saja), 1 = parsed, -1 = invalid.
 */
static int agnc_repl_parse_setting_bool(const char *arg, int current, int *value_out)
{
    if (arg == NULL || arg[0] == '\0') {
        return 0;
    }

    if (agnc_repl_str_ieq(arg, "on") || agnc_repl_str_ieq(arg, "true") || agnc_repl_str_ieq(arg, "1") ||
        agnc_repl_str_ieq(arg, "ya")) {
        *value_out = 1;
        return 1;
    }

    if (agnc_repl_str_ieq(arg, "off") || agnc_repl_str_ieq(arg, "false") || agnc_repl_str_ieq(arg, "0") ||
        agnc_repl_str_ieq(arg, "tidak")) {
        *value_out = 0;
        return 1;
    }

    if (agnc_repl_str_ieq(arg, "toggle")) {
        *value_out = current ? 0 : 1;
        return 1;
    }

    return -1;
}

static int agnc_repl_handle_slash(
    char *line,
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    char **session_path,
    char **active_session_name,
    agnc_mcp_session_t *mcp_session,
    agnc_session_usage_t *session_usage,
    long *last_usage_prompt,
    long *last_usage_completion,
    long *last_usage_total)
{
    const char *arg;
    char *space;

    if (strncmp(line, "/help", 5) == 0) {
        agnc_repl_print_help();
        return 1;
    }

    if (strncmp(line, "/cls", 4) == 0 &&
        (line[4] == '\0' || line[4] == ' ' || line[4] == '\t')) {
        agnc_console_clear_screen();
        return 1;
    }

    if (strncmp(line, "/clear", 6) == 0) {
        agnc_conversation_clear(conversation);
        if (*session_path != NULL) {
            (void)agnc_session_clear_messages(*session_path, config);
            agnc_opencode_clear_session_link(*session_path);
            (void)agnc_session_usage_reset(*session_path);
            (void)agnc_session_cost_reset(*session_path);
        }
        if (session_usage != NULL) {
            agnc_session_usage_init(session_usage);
        }
        if (last_usage_prompt != NULL) {
            *last_usage_prompt = -1;
        }
        if (last_usage_completion != NULL) {
            *last_usage_completion = -1;
        }
        if (last_usage_total != NULL) {
            *last_usage_total = -1;
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

    if (strncmp(line, "/models", 7) == 0) {
        char *provider_filter = NULL;
        char *name_filter = NULL;
        agnc_status_t parse_status;
        int show_result;

        arg = line + 7;
        agnc_console_print_chat_system("models");
        parse_status = agnc_cli_models_parse_query(arg, &provider_filter, &name_filter);
        if (parse_status != AGNC_STATUS_OK) {
            agnc_console_print_chat_system("format: /models [provider] [filter] atau --filter PATTERN");
            free(provider_filter);
            free(name_filter);
            return 1;
        }

        g_repl_cancel_flag = 0;
        g_repl_in_request = 1;
        show_result = agnc_cli_show_models(
            provider_filter,
            name_filter,
            config->provider_id,
            config->model,
            &g_repl_cancel_flag);
        g_repl_in_request = 0;
        g_repl_cancel_flag = 0;

        free(provider_filter);
        free(name_filter);
        if (show_result == 2) {
            agnc_console_print_chat_system("discovery dibatalkan");
        }
        return 1;
    }

    if (strncmp(line, "/model", 6) == 0) {
        arg = line + 6;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg == '\0') {
            const agnc_gateway_descriptor_t *gateway =
                agnc_registry_find_gateway(config->gateway_id);

            agnc_console_begin_repl_output();
            agnc_console_print_chat_system("model");
            printf(
                "  provider: %s\n  gateway:  %s\n  model:    %s\n",
                config->provider_id != NULL ? config->provider_id : "?",
                config->gateway_id != NULL ? config->gateway_id : "?",
                config->model != NULL ? config->model : "?");

            if (gateway != NULL && gateway->model_count > 0 && gateway->model_count <= 16) {
                size_t index;

                printf("  katalog statis (%zu):\n", gateway->model_count);
                for (index = 0; index < gateway->model_count; index++) {
                    const agnc_model_descriptor_t *model = &gateway->models[index];
                    const char *marker =
                        config->model != NULL && model->id != NULL && strcmp(model->id, config->model) == 0
                        ? " *"
                        : "";

                    printf("    %s%s\n", model->id != NULL ? model->id : "?", marker);
                }
            } else {
                printf("  Daftar lengkap: /models [provider] [filter]\n");
            }
            agnc_console_end_repl_output();
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
            agnc_console_begin_repl_output();
            agnc_console_print_chat_system("provider");
            agnc_repl_print_provider_status(config);
            agnc_console_end_repl_output();
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
                delete_name, session_path, active_session_name, config, conversation, session_usage);
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
                new_name, session_path, active_session_name, config, conversation, session_usage, 1, 0);
            if (switch_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("session new gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(switch_status));
            } else {
                if (last_usage_prompt != NULL) {
                    *last_usage_prompt = -1;
                }
                if (last_usage_completion != NULL) {
                    *last_usage_completion = -1;
                }
                if (last_usage_total != NULL) {
                    *last_usage_total = -1;
                }
                char detail[80];
                snprintf(detail, sizeof(detail), "sesi baru: %s", new_name);
                agnc_console_print_chat_system(detail);
            }
            return 1;
        }

        {
            agnc_status_t switch_status = agnc_repl_switch_session(
                arg, session_path, active_session_name, config, conversation, session_usage, 0, 0);

            if (switch_status != AGNC_STATUS_OK) {
                agnc_console_print_chat_system("session gagal");
                fprintf(stderr, "agnc: %s\n", agnc_status_to_string(switch_status));
            } else {
                if (last_usage_prompt != NULL) {
                    *last_usage_prompt = -1;
                }
                if (last_usage_completion != NULL) {
                    *last_usage_completion = -1;
                }
                if (last_usage_total != NULL) {
                    *last_usage_total = -1;
                }
                char detail[80];
                snprintf(detail, sizeof(detail), "sesi aktif: %s", arg);
                agnc_console_print_chat_system(detail);
            }
        }
        return 1;
    }

    if (strncmp(line, "/skills", 7) == 0) {
        arg = line + 7;
        while (*arg == ' ') {
            arg++;
        }
        if (strcmp(arg, "reload") == 0) {
            agnc_skills_invalidate();
            agnc_repl_print_skills(config, 1);
        } else {
            agnc_repl_print_skills(config, 0);
        }
        return 1;
    }

    if (strncmp(line, "/hooks", 6) == 0) {
        agnc_repl_print_hooks(config);
        return 1;
    }

    if (strncmp(line, "/cost", 5) == 0) {
        double total_usd = 0.0;
        char *formatted = NULL;

        if (*session_path != NULL) {
            (void)agnc_session_cost_load(*session_path, &total_usd);
        }
        formatted = agnc_cost_format_usd(total_usd);
        printf("Estimasi biaya sesi \"%s\": %s\n", *active_session_name != NULL ? *active_session_name : "?", formatted != NULL ? formatted : "?");
        free(formatted);
        return 1;
    }

    if (strncmp(line, "/view", 5) == 0) {
        arg = line + 5;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg == '\0' || strcmp(arg, "off") == 0) {
            agnc_console_print_chat_system("TUI panel: off");
            agnc_tui_set_view(AGNC_TUI_VIEW_NORMAL);
        } else if (strcmp(arg, "tools") == 0) {
            agnc_console_print_chat_system("TUI panel: tools");
            agnc_tui_set_view(AGNC_TUI_VIEW_TOOLS);
        } else if (strcmp(arg, "jobs") == 0) {
            agnc_console_print_chat_system("TUI panel: jobs");
            agnc_tui_set_view(AGNC_TUI_VIEW_JOBS);
        } else {
            agnc_console_print_chat_system("format: /view [tools|jobs|off]");
        }
        return 1;
    }

    if (strncmp(line, "/jobs", 5) == 0) {
        arg = line + 5;
        while (*arg == ' ') {
            arg++;
        }
        if (strcmp(arg, "cancel") == 0) {
            if (agnc_repl_job_cancel_running()) {
                agnc_console_print_chat_system("background job dibatalkan");
            } else {
                agnc_console_print_chat_system("tidak ada job background aktif");
            }
        } else if (strcmp(arg, "clear") == 0) {
            int cleared = agnc_repl_jobs_clear_queue();

            if (cleared > 0) {
                char detail[96];
                snprintf(detail, sizeof(detail), "antrean background dikosongkan (%d job)", cleared);
                agnc_console_print_chat_system(detail);
            } else {
                agnc_console_print_chat_system("antrean background sudah kosong");
            }
        } else {
            agnc_console_begin_repl_output();
            agnc_console_print_chat_system("jobs");
            agnc_repl_jobs_print_status();
            agnc_console_end_repl_output();
        }
        return 1;
    }

    if (strncmp(line, "/bg", 3) == 0) {
        arg = line + 3;
        while (*arg == ' ') {
            arg++;
        }
        if (*arg == '\0') {
            agnc_console_print_chat_system("format: /bg <prompt>");
            return 1;
        }
        agnc_console_print_chat_user(line);
        if (*session_path == NULL) {
            agnc_console_print_chat_system("background job gagal — tidak ada sesi aktif (gunakan /session)");
        } else if (agnc_repl_job_submit(
                       arg,
                       config,
                       *session_path,
                       *active_session_name,
                       0,
                       NULL,
                       NULL) != 0) {
            char detail[160];

            snprintf(
                detail,
                sizeof(detail),
                "background job gagal — antrean penuh (maks %d) atau error internal",
                AGNC_REPL_JOB_QUEUE_MAX);
            agnc_console_print_chat_system(detail);
        }
        return 1;
    }

    if (strncmp(line, "/usage", 6) == 0) {
        agnc_console_begin_repl_output();
        agnc_console_print_chat_system("usage");
        agnc_repl_print_usage_detail(
            *active_session_name,
            session_usage,
            last_usage_prompt != NULL ? *last_usage_prompt : -1,
            last_usage_completion != NULL ? *last_usage_completion : -1,
            last_usage_total != NULL ? *last_usage_total : -1);
        agnc_console_end_repl_output();
        return 1;
    }

    if (strncmp(line, "/verbose", 8) == 0) {
        int new_verbose;
        int parsed;
        char detail[160];
        agnc_status_t save_status;

        arg = line + 8;
        while (*arg == ' ') {
            arg++;
        }

        if (*arg == '\0') {
            snprintf(
                detail,
                sizeof(detail),
                "verbose: %s (runtime.verbose di ~/.agnc.json)",
                config->verbose ? "on" : "off");
            agnc_console_print_chat_system(detail);
            agnc_console_repl_printf("  gunakan: /verbose on|off|toggle\n");
            return 1;
        }

        parsed = agnc_repl_parse_setting_bool(arg, config->verbose, &new_verbose);
        if (parsed < 0) {
            agnc_console_print_chat_system("format: /verbose [on|off|toggle]");
            return 1;
        }

        save_status = agnc_config_set_runtime_verbose(NULL, new_verbose);
        if (save_status != AGNC_STATUS_OK) {
            snprintf(
                detail,
                sizeof(detail),
                "gagal menyimpan verbose (%s)",
                agnc_status_to_string(save_status));
            agnc_console_print_chat_system(detail);
            return 1;
        }

        config->verbose = new_verbose;
        snprintf(
            detail,
            sizeof(detail),
            "verbose %s (disimpan ke ~/.agnc.json)",
            new_verbose ? "on" : "off");
        agnc_console_print_chat_system(detail);
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
    agnc_session_usage_t session_usage;
    char *session_path = NULL;
    char *active_session_name = NULL;
    char line[AGNC_REPL_LINE_MAX];
    long usage_prompt = -1;
    long usage_completion = -1;
    long usage_total = -1;
    long last_usage_prompt = -1;
    long last_usage_completion = -1;
    long last_usage_total = -1;
    agnc_status_t status;
    int exit_code = 0;

    agnc_config_init(&config);
    status = agnc_config_load(NULL, &config);
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: gagal memuat config (~/.agnc.json): %s\n", agnc_status_to_string(status));
        fprintf(stderr, "agnc: periksa ~/.agnc.json atau jalankan `agnc doctor`\n");
        agnc_config_free(&config);
        return 1;
    }

    if (config.stream == 0) {
        config.stream = 1;
    }

    agnc_conversation_init(&conversation);
    agnc_mcp_session_init(&mcp_session);
    agnc_permission_session_reset();
    agnc_tool_cache_reset();
    agnc_find_symbol_index_invalidate();
    agnc_skills_invalidate();
    status = agnc_session_active_name_load(&active_session_name);
    if (status != AGNC_STATUS_OK || active_session_name == NULL) {
        free(active_session_name);
        active_session_name = agnc_strdup_local("current");
    }
    status = agnc_session_path_for_name(active_session_name, &session_path);
    if (status == AGNC_STATUS_OK && session_path != NULL) {
        /* Sisa atomic write dari proses sebelumnya (*.json.tmp.*). */
        (void)agnc_session_cleanup_stale_temp_files();
        (void)agnc_session_cleanup_legacy_bg_files();

        char *loaded_provider = NULL;
        char *loaded_model = NULL;

        if (agnc_session_load(session_path, &conversation, &loaded_provider, &loaded_model) == AGNC_STATUS_OK) {
            agnc_repl_apply_loaded_session_meta(&config, loaded_provider, loaded_model);
            agnc_repl_notify_memory_window(&conversation);
        }
    }

    agnc_session_usage_init(&session_usage);
    agnc_repl_usage_reload(&session_usage, session_path);

    if (config.hooks_enabled) {
        agnc_hook_payload_input_t hook_input;
        char *payload_json;

        memset(&hook_input, 0, sizeof(hook_input));
        hook_input.session_name = active_session_name;
        hook_input.provider_id = config.provider_id;
        hook_input.model = config.model;
        payload_json = agnc_hooks_build_payload_json(AGNC_HOOK_EVENT_SESSION_START, &hook_input);
        if (payload_json != NULL) {
            (void)agnc_hooks_run(&config, AGNC_HOOK_EVENT_SESSION_START, payload_json, NULL);
            agnc_hooks_free_payload(payload_json);
        }
    }

    agnc_repl_install_cancel_handler();
    agnc_repl_jobs_init();
    agnc_repl_line_reset_exit();
    agnc_tui_init(config.tui_enabled);
    agnc_repl_line_set_idle(
        agnc_repl_bg_idle_needed,
        agnc_repl_bg_idle_poll,
        agnc_repl_bg_idle_perm_needed,
        agnc_repl_bg_idle_perm_handle);
    agnc_repl_refresh_tui_status(&config, active_session_name, -1);
    agnc_tui_show_prompt();

    memset(&options, 0, sizeof(options));
    options.cancel_flag = &g_repl_cancel_flag;
    options.stream_live_print = 0;
    options.chat_assistant_timestamp = 1;
    options.mcp_session = &mcp_session;
    options.usage_prompt_tokens = &usage_prompt;
    options.usage_completion_tokens = &usage_completion;
    options.usage_total_tokens = &usage_total;
    options.session_name = active_session_name;
    options.session_sqlite_path = session_path;

    for (;;) {
        agnc_repl_bg_merge_bind(&conversation, session_path);
        agnc_repl_refresh_tui_status(&config, active_session_name, last_usage_total);

        if (agnc_repl_jobs_poll(&conversation, session_path)) {
            agnc_tui_show_prompt();
        }

        if (!agnc_repl_read_line(line, sizeof(line))) {
            break;
        }

        if (agnc_repl_jobs_poll(&conversation, session_path)) {
            agnc_tui_show_prompt();
        }

        agnc_repl_trim(line);
        if (line[0] == '\0') {
            continue;
        }

        if (line[0] == '&') {
            const char *bg_prompt = line + 1;

            while (*bg_prompt == ' ') {
                bg_prompt++;
            }
            if (*bg_prompt != '\0') {
                agnc_console_clear_input_line();
                agnc_console_print_chat_user(line);
                if (session_path != NULL) {
                    (void)agnc_repl_job_submit(
                        bg_prompt,
                        &config,
                        session_path,
                        active_session_name,
                        0,
                        NULL,
                        NULL);
                } else {
                    agnc_console_print_chat_system("background job gagal — tidak ada sesi aktif");
                }
            }
            agnc_tui_show_prompt();
            continue;
        }

        if (line[0] == '/') {
            agnc_console_clear_input_line();
            {
                int slash_result = agnc_repl_handle_slash(
                    line,
                    &config,
                    &conversation,
                    &session_path,
                    &active_session_name,
                    &mcp_session,
                    &session_usage,
                    &last_usage_prompt,
                    &last_usage_completion,
                    &last_usage_total);
                if (slash_result == 2) {
                    break;
                }
            }
            agnc_tui_show_prompt();
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
            if (agnc_tui_is_active()) {
                agnc_tui_set_toast("request dibatalkan");
            } else {
                agnc_console_print_chat_system("request dibatalkan");
            }
            agnc_tui_show_prompt();
            continue;
        }

        if (status == AGNC_STATUS_OK) {
            last_usage_prompt = usage_prompt;
            last_usage_completion = usage_completion;
            last_usage_total = usage_total;
            if (session_path != NULL) {
                (void)agnc_session_usage_accumulate(
                    session_path, usage_prompt, usage_completion, usage_total);
                agnc_repl_usage_reload(&session_usage, session_path);
            }
            agnc_repl_print_usage_summary(&session_usage, usage_prompt, usage_completion, usage_total);
        }

        if (session_path != NULL) {
            (void)agnc_session_sync(session_path, &conversation, &config);
        }

        agnc_tui_show_prompt();
    }

    if (session_path != NULL) {
        (void)agnc_session_sync(session_path, &conversation, &config);
    }

    free(session_path);
    free(active_session_name);
    agnc_tui_shutdown();
    agnc_console_clear_screen();
    agnc_mcp_session_free(&mcp_session);
    agnc_repl_jobs_shutdown();
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    return exit_code;
}
