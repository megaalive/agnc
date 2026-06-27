/*
 * doctor.c
 *
 * Implementasi perintah `agnc doctor`.
 * Memeriksa kondisi lingkungan runtime: platform, config, folder kunci dev,
 * dan ketersediaan dependency yang akan dipakai di fase berikutnya.
 */

#include "agnc/cli.h"
#include "agnc/config.h"
#include "agnc/mcp/registry.h"
#include "agnc/path.h"
#include "agnc/provider.h"
#include "agnc/rg_locate.h"
#include "agnc/ctags_locate.h"
#include "agnc/status.h"
#include "agnc/tool_path.h"
#include "agnc/ollama.h"
#include "agnc/opencode.h"
#include "agnc/skills.h"
#include "agnc/hooks.h"
#include "agnc/version.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

#include <yyjson.h>

/*
 * Mengembalikan nama platform compile-time.
 * String ini membantu diagnosis perbedaan perilaku Windows vs Unix.
 */
static const char *agnc_platform_name(void)
{
#if defined(AGNC_PLATFORM_WINDOWS)
    return "windows";
#elif defined(AGNC_PLATFORM_MACOS)
    return "macos";
#elif defined(AGNC_PLATFORM_LINUX)
    return "linux";
#else
    return "unknown";
#endif
}

/*
 * Mencetak satu baris hasil pemeriksaan dengan format kolom tetap
 * agar output doctor mudah dibaca di terminal.
 */
static void agnc_doctor_print_status(const char *name, const char *status, const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        printf("  %-18s %-10s %s\n", name, status, detail);
        return;
    }

    printf("  %-18s %s\n", name, status);
}

/*
 * Menjalankan seluruh rangkaian health check Fase 0.
 * Exit code selalu 0 karena "missing config" belum dianggap fatal.
 */
int agnc_cli_run_doctor(void)
{
    char *config_path = NULL;
    agnc_status_t status;

    printf("agnc doctor\n");
    printf("Platform: %s\n", agnc_platform_name());
    printf("Version:  %s\n", AGNC_VERSION_STRING);
    printf("\nChecks:\n");

    /* Periksa apakah file config default (~/.agnc.json) sudah ada. */
    status = agnc_path_default_config(&config_path);
    if (status != AGNC_STATUS_OK) {
        agnc_doctor_print_status("config_path", "error", agnc_status_to_string(status));
    } else if (agnc_path_exists(config_path)) {
        agnc_doctor_print_status("config_path", "ok", config_path);
    } else {
        /*
         * Config belum ada bukan error fatal di Fase 0.
         * Pengguna baru bisa mulai tanpa file config sampai Fase 1 siap.
         */
        agnc_doctor_print_status("config_path", "missing", config_path);
    }

    /*
     * Folder .keys/ hanya untuk development lokal.
     * Keberadaannya diingatkan agar tidak pernah ikut commit ke git.
     */
    if (agnc_path_exists(".keys")) {
        agnc_doctor_print_status("keys_dir", "present", "local only, must stay gitignored");
    } else {
        agnc_doctor_print_status("keys_dir", "missing", "optional for dev");
    }

    /* libcurl dan yyjson sudah di-link di Fase 1. */
    agnc_doctor_print_status("libcurl", "ok", curl_version());
    agnc_doctor_print_status("yyjson", "ok", YYJSON_VERSION_STRING);

    printf("  %-18s %-10s %zu gateway(s) registered\n", "provider_registry", "ok", agnc_registry_gateway_count());

    /* ripgrep diperlukan tool grep (Fase 2). */
    {
        const char *rg_path = agnc_rg_locate_binary();

        if (rg_path != NULL && rg_path[0] != '\0') {
            agnc_doctor_print_status("ripgrep", "ok", rg_path);
        } else {
            agnc_doctor_print_status("ripgrep", "missing", "install ripgrep for grep tool");
        }
    }

    /* Tool workspace (read_file/grep/glob) — terpisah dari root MCP. */
    {
        char *workspace_root = NULL;
        const char *workspace_env = getenv("AGNC_WORKSPACE");

        if (agnc_tool_path_workspace_root(&workspace_root) == AGNC_STATUS_OK && workspace_root != NULL) {
            if (workspace_env != NULL && workspace_env[0] != '\0') {
                char detail[512];
                snprintf(
                    detail,
                    sizeof(detail),
                    "%s (AGNC_WORKSPACE=%s)",
                    workspace_root,
                    workspace_env);
                agnc_doctor_print_status("tool_workspace", "ok", detail);
            } else {
                agnc_doctor_print_status("tool_workspace", "ok", workspace_root);
            }
            free(workspace_root);
        } else {
            agnc_doctor_print_status("tool_workspace", "error", "cannot resolve workspace root");
        }
    }

    /* Universal Ctags diperlukan tool find_symbol (Fase 6.10). */
    {
        const char *ctags_path = agnc_ctags_locate_binary();

        if (ctags_path != NULL && ctags_path[0] != '\0') {
            agnc_doctor_print_status("ctags", "ok", ctags_path);
        } else {
            agnc_doctor_print_status("ctags", "missing", "install Universal Ctags for find_symbol");
        }
    }

    /* Ollama lokal (opsional) — OpenAI-compatible di :11434/v1. */
    {
        char detail[256];
        size_t model_count = 0;
        agnc_status_t ollama_status = agnc_ollama_probe(
            AGNC_OLLAMA_DEFAULT_BASE_URL, &model_count, detail, sizeof(detail));

        if (ollama_status == AGNC_STATUS_OK) {
            agnc_doctor_print_status("ollama", model_count > 0 ? "ok" : "missing", detail);
        } else {
            agnc_doctor_print_status("ollama", "missing", "start Ollama (ollama serve) or install from ollama.com");
        }
    }

    /* OpenCode lokal (opsional) — native API di :4096 (opencode serve). */
    {
        char detail[256];
        agnc_status_t opencode_status =
            agnc_opencode_probe(AGNC_OPENCODE_DEFAULT_BASE_URL, detail, sizeof(detail));

        if (opencode_status == AGNC_STATUS_OK) {
            agnc_doctor_print_status("opencode", "ok", detail);
        } else {
            agnc_doctor_print_status("opencode", "missing", "start OpenCode (opencode serve) on :4096");
        }
    }

    /* Validasi config bisa dimuat jika file ada. */
    if (config_path != NULL && agnc_path_exists(config_path)) {
        agnc_config_t config;
        char detail[256];

        agnc_config_init(&config);
        if (agnc_config_load(config_path, &config) == AGNC_STATUS_OK) {
            const agnc_gateway_descriptor_t *gateway = agnc_registry_find_gateway(config.gateway_id);
            snprintf(
                detail,
                sizeof(detail),
                "%s via %s (%s)",
                config.model != NULL ? config.model : "?",
                config.gateway_id != NULL ? config.gateway_id : "?",
                gateway != NULL && gateway->label != NULL ? gateway->label : "?");
            agnc_doctor_print_status("config_load", "ok", detail);
            snprintf(
                detail,
                sizeof(detail),
                "active=%s gateway=%s",
                config.provider_id != NULL ? config.provider_id : "?",
                config.gateway_id != NULL ? config.gateway_id : "?");
            agnc_doctor_print_status("provider_active", "ok", detail);

            if (!config.skills_enabled) {
                agnc_doctor_print_status("skills", "skipped", "disabled in config");
            } else {
                agnc_skill_entry_t *entries = NULL;
                size_t skill_count = 0;

                if (agnc_skills_list(&config, &entries, &skill_count) == AGNC_STATUS_OK) {
                    snprintf(detail, sizeof(detail), "%zu skill file(s) loaded", skill_count);
                    agnc_doctor_print_status("skills", skill_count > 0 ? "ok" : "missing", detail);
                    agnc_skills_list_free(entries, skill_count);
                } else {
                    agnc_doctor_print_status("skills", "error", "cannot scan skill folders");
                }
            }

            if (!config.hooks_enabled) {
                agnc_doctor_print_status("hooks", "skipped", "disabled in config");
            } else {
                size_t hook_total = 0;
                size_t event_index;

                for (event_index = 0; event_index < (size_t)AGNC_HOOK_EVENT_COUNT; event_index++) {
                    hook_total += agnc_hooks_count_for_event(&config, (agnc_hook_event_id_t)event_index);
                }

                snprintf(detail, sizeof(detail), "%zu command(s) across %d events", hook_total, AGNC_HOOK_EVENT_COUNT);
                agnc_doctor_print_status("hooks", hook_total > 0 ? "ok" : "missing", detail);
            }

            if (config.mcp_server_count > 0) {
                size_t server_index;
                size_t enabled_count = 0;

                for (server_index = 0; server_index < config.mcp_server_count; server_index++) {
                    if (config.mcp_servers[server_index].enabled) {
                        enabled_count++;
                    }
                }

                snprintf(
                    detail,
                    sizeof(detail),
                    "%zu configured, %zu enabled",
                    config.mcp_server_count,
                    enabled_count);
                agnc_doctor_print_status("mcp_config", "ok", detail);

                if (enabled_count > 0) {
                    agnc_mcp_registry_t registry;

                    agnc_mcp_registry_init(&registry);
                    if (agnc_mcp_registry_load_from_config(&config, &registry, 5000) == AGNC_STATUS_OK) {
                        snprintf(
                            detail,
                            sizeof(detail),
                            "%zu/%zu server(s) connected",
                            agnc_mcp_registry_server_count(&registry),
                            enabled_count);
                        agnc_doctor_print_status("mcp_connect", "ok", detail);
                    } else {
                        agnc_doctor_print_status("mcp_connect", "error", "no enabled MCP server connected");
                    }
                    agnc_mcp_registry_free(&registry);
                }
            } else {
                agnc_doctor_print_status("mcp_config", "skipped", "no mcp.servers in config");
            }
        } else {
            agnc_doctor_print_status("config_load", "error", "invalid config or missing API key env");
        }
        agnc_config_free(&config);
    } else {
        agnc_doctor_print_status("config_load", "skipped", "config file missing");
    }

    /* Cek env API key tanpa menampilkan nilainya. */
    if (getenv("AGNC_API_KEY") != NULL) {
        agnc_doctor_print_status("api_key_env", "ok", "present (value hidden)");
    } else {
        agnc_doctor_print_status("api_key_env", "missing", "set AGNC_API_KEY");
    }

    free(config_path);
    return 0;
}
