/*
 * models.c
 *
 * Discovery model untuk semua provider di config (~/.agnc.json).
 */

#include "agnc/cli.h"
#include "agnc/config.h"
#include "agnc/provider.h"
#include "agnc/status.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AGNC_MODELS_DISPLAY_MAX 48
#define AGNC_MODELS_FILTERED_DISPLAY_MAX 256

static int agnc_cli_model_name_matches(const char *model_id, const char *filter)
{
    size_t filter_len;
    size_t index;
    size_t offset;

    if (filter == NULL || filter[0] == '\0') {
        return 1;
    }
    if (model_id == NULL) {
        return 0;
    }

    filter_len = strlen(filter);
    for (index = 0; model_id[index] != '\0'; index++) {
        for (offset = 0; offset < filter_len; offset++) {
            unsigned char model_ch = (unsigned char)model_id[index + offset];
            unsigned char filter_ch = (unsigned char)filter[offset];

            if (model_ch == '\0') {
                break;
            }

            model_ch = (unsigned char)tolower(model_ch);
            filter_ch = (unsigned char)tolower(filter_ch);
            if (model_ch != filter_ch) {
                break;
            }
        }
        if (offset == filter_len) {
            return 1;
        }
    }

    return 0;
}

static size_t agnc_cli_count_matching_models(
    const agnc_provider_models_snapshot_t *snapshot,
    const char *name_filter)
{
    size_t index;
    size_t matched = 0;

    if (snapshot == NULL || snapshot->model_ids == NULL) {
        return 0;
    }

    for (index = 0; index < snapshot->model_count; index++) {
        if (agnc_cli_model_name_matches(snapshot->model_ids[index], name_filter)) {
            matched++;
        }
    }

    return matched;
}

static void agnc_cli_print_models_text(
    const agnc_provider_models_snapshot_t *snapshots,
    size_t snapshot_count,
    const char *active_provider_id,
    const char *active_model,
    const char *name_filter)
{
    size_t provider_index;
    int has_filter = name_filter != NULL && name_filter[0] != '\0';

    if (snapshots == NULL || snapshot_count == 0) {
        printf("Tidak ada provider di config (providers{}).\n");
        return;
    }

    for (provider_index = 0; provider_index < snapshot_count; provider_index++) {
        const agnc_provider_models_snapshot_t *snapshot = &snapshots[provider_index];
        size_t model_index;
        size_t matched_count;
        size_t shown_count = 0;
        size_t display_max =
            has_filter ? AGNC_MODELS_FILTERED_DISPLAY_MAX : AGNC_MODELS_DISPLAY_MAX;
        int is_active_provider =
            active_provider_id != NULL && snapshot->provider_id != NULL &&
            strcmp(active_provider_id, snapshot->provider_id) == 0;

        printf(
            "[%s] gateway=%s base=%s default=%s\n",
            snapshot->provider_id != NULL ? snapshot->provider_id : "?",
            snapshot->gateway_id != NULL ? snapshot->gateway_id : "?",
            snapshot->base_url != NULL ? snapshot->base_url : "?",
            snapshot->default_model != NULL ? snapshot->default_model : "?");

        if (snapshot->status != AGNC_STATUS_OK) {
            printf(
                "  (error: %s)\n",
                snapshot->error_message != NULL ? snapshot->error_message
                                                : agnc_status_to_string(snapshot->status));
            continue;
        }

        if (snapshot->model_count == 0) {
            printf("  (no models)\n");
            continue;
        }

        matched_count = has_filter ? agnc_cli_count_matching_models(snapshot, name_filter)
                                   : snapshot->model_count;

        if (has_filter) {
            printf(
                "  models (%zu/%zu matching '%s'):\n",
                matched_count,
                snapshot->model_count,
                name_filter);
        } else {
            printf("  models (%zu):\n", snapshot->model_count);
        }

        if (matched_count == 0) {
            printf("    (no match)\n");
            continue;
        }

        for (model_index = 0; model_index < snapshot->model_count; model_index++) {
            const char *model_id = snapshot->model_ids[model_index];
            const char *marker;

            if (!agnc_cli_model_name_matches(model_id, name_filter)) {
                continue;
            }

            if (shown_count >= display_max) {
                continue;
            }

            marker =
                is_active_provider && active_model != NULL && model_id != NULL &&
                        strcmp(model_id, active_model) == 0
                    ? " *"
                    : "";

            printf("    %s%s\n", model_id != NULL ? model_id : "?", marker);
            shown_count++;
        }

        if (matched_count > display_max) {
            printf("    ... (%zu more — perketat filter)\n", matched_count - display_max);
        } else if (!has_filter && snapshot->model_count > display_max) {
            printf("    ... (%zu more — gunakan --filter)\n", snapshot->model_count - display_max);
        }
    }
}

static void agnc_cli_print_models_json(
    const agnc_provider_models_snapshot_t *snapshots,
    size_t snapshot_count,
    const char *name_filter)
{
    size_t provider_index;
    size_t model_index;

    printf("{\"providers\":[");
    for (provider_index = 0; provider_index < snapshot_count; provider_index++) {
        const agnc_provider_models_snapshot_t *snapshot = &snapshots[provider_index];
        int first_model = 1;

        if (provider_index > 0) {
            printf(",");
        }

        printf(
            "{\"id\":\"%s\",\"gateway\":\"%s\",\"base_url\":\"%s\",\"default_model\":\"%s\",\"status\":\"%s\"",
            snapshot->provider_id != NULL ? snapshot->provider_id : "",
            snapshot->gateway_id != NULL ? snapshot->gateway_id : "",
            snapshot->base_url != NULL ? snapshot->base_url : "",
            snapshot->default_model != NULL ? snapshot->default_model : "",
            agnc_status_to_string(snapshot->status));

        if (snapshot->error_message != NULL) {
            printf(",\"error\":\"");
            for (model_index = 0; snapshot->error_message[model_index] != '\0'; model_index++) {
                char ch = snapshot->error_message[model_index];
                if (ch == '\\' || ch == '"') {
                    putchar('\\');
                }
                putchar(ch);
            }
            printf("\"");
        }

        printf(",\"models\":[");
        if (snapshot->status == AGNC_STATUS_OK) {
            for (model_index = 0; model_index < snapshot->model_count; model_index++) {
                const char *model_id = snapshot->model_ids[model_index];

                if (!agnc_cli_model_name_matches(model_id, name_filter)) {
                    continue;
                }

                if (!first_model) {
                    printf(",");
                }
                first_model = 0;

                printf("\"");
                if (model_id != NULL) {
                    size_t char_index;
                    for (char_index = 0; model_id[char_index] != '\0'; char_index++) {
                        char ch = model_id[char_index];
                        if (ch == '\\' || ch == '"') {
                            putchar('\\');
                        }
                        putchar(ch);
                    }
                }
                printf("\"");
            }
        }
        printf("]}");
    }
    printf("]}\n");
}

static void agnc_cli_print_models_header(
    const char *provider_id_filter,
    const char *name_filter,
    int json_output)
{
    if (json_output) {
        return;
    }

    if (provider_id_filter != NULL && provider_id_filter[0] != '\0') {
        if (name_filter != NULL && name_filter[0] != '\0') {
            printf(
                "Model discovery — provider %s, filter '%s'\n\n",
                provider_id_filter,
                name_filter);
        } else {
            printf("Model discovery — provider %s\n\n", provider_id_filter);
        }
        return;
    }

    if (name_filter != NULL && name_filter[0] != '\0') {
        printf("Model discovery — filter '%s'\n\n", name_filter);
        return;
    }

    printf("Model discovery — providers di ~/.agnc.json\n\n");
}

int agnc_cli_run_models(const char *provider_id_filter, const char *name_filter, int json_output)
{
    agnc_config_t config;
    agnc_provider_models_snapshot_t *snapshots = NULL;
    size_t snapshot_count = 0;
    agnc_status_t status;

    agnc_config_init(&config);
    status = agnc_config_load(NULL, &config);
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: gagal memuat config: %s\n", agnc_status_to_string(status));
        agnc_config_free(&config);
        return 1;
    }

    status = agnc_provider_discover_configured(NULL, provider_id_filter, &snapshots, &snapshot_count, NULL);
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: model discovery gagal: %s\n", agnc_status_to_string(status));
        agnc_config_free(&config);
        return 1;
    }

    agnc_cli_print_models_header(provider_id_filter, name_filter, json_output);

    if (json_output) {
        agnc_cli_print_models_json(snapshots, snapshot_count, name_filter);
    } else {
        agnc_cli_print_models_text(
            snapshots,
            snapshot_count,
            config.provider_id,
            config.model,
            name_filter);
    }

    agnc_provider_models_snapshots_free(snapshots, snapshot_count);
    agnc_config_free(&config);
    return 0;
}

int agnc_cli_show_models(
    const char *provider_id_filter,
    const char *name_filter,
    const char *active_provider_id,
    const char *active_model,
    volatile int *cancel_flag)
{
    agnc_provider_models_snapshot_t *snapshots = NULL;
    size_t snapshot_count = 0;
    agnc_status_t status;

    status = agnc_provider_discover_configured(
        NULL, provider_id_filter, &snapshots, &snapshot_count, cancel_flag);
    if (status == AGNC_STATUS_CANCELLED) {
        return 2;
    }
    if (status != AGNC_STATUS_OK) {
        fprintf(stderr, "agnc: model discovery gagal: %s\n", agnc_status_to_string(status));
        return 1;
    }

    agnc_cli_print_models_header(provider_id_filter, name_filter, 0);
    agnc_cli_print_models_text(
        snapshots,
        snapshot_count,
        active_provider_id,
        active_model,
        name_filter);
    agnc_provider_models_snapshots_free(snapshots, snapshot_count);
    return 0;
}

static char *agnc_cli_models_parse_dup_token(const char **cursor)
{
    const char *start;
    size_t length;
    char *copy;

    while (**cursor == ' ') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return NULL;
    }

    start = *cursor;
    while (**cursor != '\0' && **cursor != ' ') {
        (*cursor)++;
    }

    length = (size_t)(*cursor - start);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

/*
 * Parse sisa baris REPL setelah "/models".
 * Mendukung: [provider] [filter], --filter PATTERN, -f PATTERN.
 * Pemanggil wajib free() provider_out dan name_filter_out.
 */
agnc_status_t agnc_cli_models_parse_query(
    const char *args,
    char **provider_out,
    char **name_filter_out)
{
    const char *cursor;
    char *provider = NULL;
    char *name_filter = NULL;
    char *token;

    if (provider_out == NULL || name_filter_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *provider_out = NULL;
    *name_filter_out = NULL;

    cursor = args != NULL ? args : "";
    while (*cursor == ' ') {
        cursor++;
    }

    while (*cursor != '\0') {
        token = agnc_cli_models_parse_dup_token(&cursor);
        if (token == NULL) {
            free(provider);
            free(name_filter);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }

        if (strcmp(token, "--filter") == 0 || strcmp(token, "-f") == 0) {
            free(token);
            token = agnc_cli_models_parse_dup_token(&cursor);
            if (token == NULL || token[0] == '\0') {
                free(token);
                free(provider);
                free(name_filter);
                return AGNC_STATUS_INVALID_ARGUMENT;
            }
            free(name_filter);
            name_filter = token;
            continue;
        }

        if (provider == NULL) {
            provider = token;
        } else if (name_filter == NULL) {
            name_filter = token;
        } else {
            free(token);
            free(provider);
            free(name_filter);
            return AGNC_STATUS_INVALID_ARGUMENT;
        }
    }

    *provider_out = provider;
    *name_filter_out = name_filter;
    return AGNC_STATUS_OK;
}
