/*
 * provider.h
 *
 * Descriptor gateway provider dan registry runtime (Fase 3).
 * Struct agnc_gateway_descriptor_t diisi dari generated/agnc_integrations_gen.c.
 */

#ifndef AGNC_PROVIDER_H
#define AGNC_PROVIDER_H

#include "agnc/config.h"
#include "agnc/status.h"

#include <stddef.h>

typedef enum {
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    AGNC_TRANSPORT_GEMINI_NATIVE,
    AGNC_TRANSPORT_LOCAL,
    AGNC_TRANSPORT_OPENCODE_NATIVE
} agnc_transport_kind_t;

typedef struct {
    const char *id;
    const char *api_name;
    int supports_streaming;
    int supports_tool_calls;
    int supports_reasoning;
} agnc_model_descriptor_t;

typedef struct {
    const char *id;
    const char *label;
    const char *default_base_url;
    const char *default_model;
    agnc_transport_kind_t transport_kind;
    const char *auth_header_name;
    const char *auth_header_scheme;
    const char *endpoint_path;
    const char *models_endpoint_path;
    int supports_streaming;
    int supports_tool_calls;
    int requires_auth;
    const char *const *credential_env_vars;
    size_t credential_env_count;
    const agnc_model_descriptor_t *models;
    size_t model_count;
} agnc_gateway_descriptor_t;

/* Cari gateway by id; NULL jika tidak dikenal. */
const agnc_gateway_descriptor_t *agnc_registry_find_gateway(const char *id);

/* Jumlah gateway terdaftar. */
size_t agnc_registry_gateway_count(void);

/* Susun URL chat completions dari base_url + descriptor. Pemanggil free() hasilnya. */
char *agnc_provider_build_chat_url(const agnc_gateway_descriptor_t *gateway, const char *base_url);

/* Susun URL GET /models untuk discovery. Pemanggil free() hasilnya. */
char *agnc_provider_build_models_url(const agnc_gateway_descriptor_t *gateway, const char *base_url);

/* Susun header Authorization (atau sesuai descriptor). Pemanggil free() hasilnya. */
char *agnc_provider_build_auth_header(const agnc_gateway_descriptor_t *gateway, const char *api_key);

/* Map id katalog ke api_name; jika tidak ada, kembalikan model apa adanya. */
const char *agnc_provider_resolve_api_model(const agnc_gateway_descriptor_t *gateway, const char *model);

/*
 * Ambil daftar model untuk provider aktif (API dinamis atau katalog gateway).
 * model_ids dan isinya dialokasikan; pemanggil memanggil agnc_provider_free_model_list().
 */
agnc_status_t agnc_provider_list_models(
    const agnc_config_t *config,
    char ***model_ids,
    size_t *model_count,
    volatile int *cancel_flag);

void agnc_provider_free_model_list(char **model_ids, size_t model_count);

typedef struct {
    char *provider_id;
    char *gateway_id;
    char *base_url;
    char *default_model;
    agnc_status_t status;
    char *error_message;
    char **model_ids;
    size_t model_count;
} agnc_provider_models_snapshot_t;

void agnc_provider_models_snapshots_free(agnc_provider_models_snapshot_t *snapshots, size_t count);

/*
 * Discovery model untuk semua (atau satu) provider di ~/.agnc.json.
 * provider_id_filter NULL = semua entri providers{}.
 */
agnc_status_t agnc_provider_discover_configured(
    const char *config_path,
    const char *provider_id_filter,
    agnc_provider_models_snapshot_t **snapshots_out,
    size_t *snapshot_count_out,
    volatile int *cancel_flag);

#endif /* AGNC_PROVIDER_H */
