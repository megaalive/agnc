/*
 * oauth.c
 *
 * Token OAuth/API tersimpan di ~/.agnc/oauth/<provider_id>.json.
 * Refresh otomatis via agnc_oauth_refresh_if_needed sebelum HTTP ke Anthropic.
 */

#include "agnc/oauth.h"

#include "agnc/atomic_write.h"
#include "agnc/net/http.h"
#include "agnc/path.h"
#include "agnc/version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <yyjson.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_oauth_token_init(agnc_oauth_token_t *token)
{
    if (token == NULL) {
        return;
    }

    token->access_token = NULL;
    token->refresh_token = NULL;
    token->expires_at = 0;
}

void agnc_oauth_token_free(agnc_oauth_token_t *token)
{
    if (token == NULL) {
        return;
    }

    free(token->access_token);
    free(token->refresh_token);
    token->access_token = NULL;
    token->refresh_token = NULL;
    token->expires_at = 0;
}

agnc_status_t agnc_oauth_token_path(const char *provider_id, char **path_out)
{
    char *template_path = NULL;
    char *path = NULL;
    size_t length;

    if (provider_id == NULL || provider_id[0] == '\0' || path_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *path_out = NULL;

    if (agnc_path_expand_user("~/.agnc/oauth", &template_path) != AGNC_STATUS_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

    length = strlen(template_path) + strlen(provider_id) + strlen(".json") + 2;
    path = (char *)malloc(length);
    free(template_path);
    if (path == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    snprintf(path, length, "~/.agnc/oauth/%s.json", provider_id);
    template_path = path;
    path = NULL;
    if (agnc_path_expand_user(template_path, &path) != AGNC_STATUS_OK) {
        free(template_path);
        return AGNC_STATUS_IO_ERROR;
    }
    free(template_path);
    *path_out = path;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_oauth_ensure_dir(void)
{
    char *dir = NULL;

    if (agnc_path_expand_user("~/.agnc/oauth", &dir) != AGNC_STATUS_OK) {
        return AGNC_STATUS_IO_ERROR;
    }

#ifdef _WIN32
    {
        char *parent = NULL;
        if (agnc_path_expand_user("~/.agnc", &parent) == AGNC_STATUS_OK && parent != NULL) {
            if (!agnc_path_exists(parent)) {
                (void)_mkdir(parent);
            }
            free(parent);
        }
        if (!agnc_path_exists(dir)) {
            if (_mkdir(dir) != 0 && errno != EEXIST) {
                free(dir);
                return AGNC_STATUS_IO_ERROR;
            }
        }
    }
#else
    if (!agnc_path_exists(dir)) {
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
            free(dir);
            return AGNC_STATUS_IO_ERROR;
        }
    }
#endif

    free(dir);
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_oauth_token_load(const char *provider_id, agnc_oauth_token_t *token_out)
{
    char *path = NULL;
    FILE *handle;
    long file_size;
    char *buffer = NULL;
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *value;
    agnc_status_t status = AGNC_STATUS_OK;

    if (token_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_oauth_token_init(token_out);

    status = agnc_oauth_token_path(provider_id, &path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    handle = fopen(path, "rb");
    free(path);
    if (handle == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return AGNC_STATUS_IO_ERROR;
    }

    file_size = ftell(handle);
    if (file_size < 0) {
        fclose(handle);
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(handle);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (fread(buffer, 1, (size_t)file_size, handle) != (size_t)file_size) {
        free(buffer);
        fclose(handle);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer[file_size] = '\0';
    fclose(handle);

    doc = yyjson_read(buffer, (size_t)file_size, 0);
    free(buffer);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    value = yyjson_obj_get(root, "access_token");
    if (value != NULL && yyjson_is_str(value)) {
        token_out->access_token = agnc_strdup_local(yyjson_get_str(value));
    }

    value = yyjson_obj_get(root, "refresh_token");
    if (value != NULL && yyjson_is_str(value)) {
        token_out->refresh_token = agnc_strdup_local(yyjson_get_str(value));
    }

    value = yyjson_obj_get(root, "expires_at");
    if (value != NULL && yyjson_is_num(value)) {
        token_out->expires_at = (long)yyjson_get_num(value);
    }

    yyjson_doc_free(doc);

    if (token_out->access_token == NULL || token_out->access_token[0] == '\0') {
        agnc_oauth_token_free(token_out);
        return AGNC_STATUS_IO_ERROR;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_oauth_token_save(const char *provider_id, const agnc_oauth_token_t *token)
{
    char *path = NULL;
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    char *json_text = NULL;
    agnc_status_t status;

    if (provider_id == NULL || token == NULL || token->access_token == NULL || token->access_token[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    status = agnc_oauth_token_path(provider_id, &path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (agnc_oauth_ensure_dir() != AGNC_STATUS_OK) {
        free(path);
        return AGNC_STATUS_IO_ERROR;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        free(path);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "access_token", token->access_token);
    if (token->refresh_token != NULL && token->refresh_token[0] != '\0') {
        yyjson_mut_obj_add_str(doc, root, "refresh_token", token->refresh_token);
    }
    if (token->expires_at > 0) {
        yyjson_mut_obj_add_int(doc, root, "expires_at", token->expires_at);
    }

    json_text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(doc);
    if (json_text == NULL) {
        free(path);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_atomic_write_file(path, json_text, strlen(json_text));
    free(json_text);
    free(path);
    return status;
}

agnc_status_t agnc_oauth_token_delete(const char *provider_id)
{
    char *path = NULL;
    agnc_status_t status;

    status = agnc_oauth_token_path(provider_id, &path);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

#ifdef _WIN32
    if (DeleteFileA(path) == 0) {
        free(path);
        return AGNC_STATUS_IO_ERROR;
    }
#else
    if (remove(path) != 0) {
        free(path);
        return AGNC_STATUS_IO_ERROR;
    }
#endif

    free(path);
    return AGNC_STATUS_OK;
}

#define AGNC_OAUTH_ANTHROPIC_CLIENT_ID "9d1c250a-e61b-44d9-88ed-5944d1962f5e"

static const char *AGNC_OAUTH_ANTHROPIC_TOKEN_URLS[] = {
    "https://platform.claude.com/v1/oauth/token",
    "https://console.anthropic.com/v1/oauth/token",
    NULL};

long agnc_oauth_token_seconds_until_expiry_at(const agnc_oauth_token_t *token, long now_unix)
{
    if (token == NULL || token->expires_at <= 0) {
        return -1;
    }

    return token->expires_at - now_unix;
}

int agnc_oauth_token_needs_refresh_at(const agnc_oauth_token_t *token, long now_unix, int force)
{
    long seconds_left;

    if (token == NULL || token->refresh_token == NULL || token->refresh_token[0] == '\0') {
        return 0;
    }

    if (token->expires_at <= 0) {
        return force ? 1 : 0;
    }

    if (force) {
        return 1;
    }

    seconds_left = agnc_oauth_token_seconds_until_expiry_at(token, now_unix);
    if (seconds_left < 0) {
        return 1;
    }

    return seconds_left <= AGNC_OAUTH_ADVISORY_REFRESH_SECONDS ? 1 : 0;
}

agnc_status_t agnc_oauth_parse_refresh_response(
    const char *json_body,
    const agnc_oauth_token_t *previous,
    agnc_oauth_token_t *updated_out,
    long now_unix)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *value;
    const char *access_text;
    long expires_in = 0;

    if (json_body == NULL || updated_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_oauth_token_init(updated_out);

    doc = yyjson_read(json_body, strlen(json_body), 0);
    if (doc == NULL) {
        return AGNC_STATUS_JSON_ERROR;
    }

    root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    value = yyjson_obj_get(root, "access_token");
    if (value == NULL || !yyjson_is_str(value)) {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    access_text = yyjson_get_str(value);
    if (access_text[0] == '\0') {
        yyjson_doc_free(doc);
        return AGNC_STATUS_JSON_ERROR;
    }

    updated_out->access_token = agnc_strdup_local(access_text);

    value = yyjson_obj_get(root, "refresh_token");
    if (value != NULL && yyjson_is_str(value) && yyjson_get_str(value)[0] != '\0') {
        updated_out->refresh_token = agnc_strdup_local(yyjson_get_str(value));
    } else if (previous != NULL && previous->refresh_token != NULL) {
        updated_out->refresh_token = agnc_strdup_local(previous->refresh_token);
    }

    value = yyjson_obj_get(root, "expires_in");
    if (value != NULL && yyjson_is_num(value)) {
        expires_in = (long)yyjson_get_num(value);
    }

    if (expires_in > 0) {
        updated_out->expires_at = now_unix + expires_in;
    } else if (previous != NULL && previous->expires_at > 0) {
        updated_out->expires_at = previous->expires_at;
    }

    yyjson_doc_free(doc);

    if (updated_out->access_token == NULL) {
        agnc_oauth_token_free(updated_out);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (updated_out->refresh_token == NULL && previous != NULL && previous->refresh_token != NULL) {
        updated_out->refresh_token = agnc_strdup_local(previous->refresh_token);
        if (updated_out->refresh_token == NULL) {
            agnc_oauth_token_free(updated_out);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_oauth_build_refresh_request(const char *refresh_token, const char *client_id, char **json_out)
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    char *json_text;

    if (refresh_token == NULL || client_id == NULL || json_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *json_out = NULL;
    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "grant_type", "refresh_token");
    yyjson_mut_obj_add_str(doc, root, "refresh_token", refresh_token);
    yyjson_mut_obj_add_str(doc, root, "client_id", client_id);

    json_text = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    if (json_text == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    *json_out = json_text;
    return AGNC_STATUS_OK;
}

static int agnc_oauth_provider_is_anthropic(const char *provider_id)
{
    return provider_id != NULL && strcmp(provider_id, "anthropic") == 0;
}

static agnc_status_t agnc_oauth_refresh_anthropic(const agnc_oauth_token_t *current, agnc_oauth_token_t *refreshed_out)
{
    char *request_json = NULL;
    char user_agent[64];
    char extra_header[96];
    size_t url_index;
    agnc_status_t status = AGNC_STATUS_HTTP_ERROR;
    long now_unix = (long)time(NULL);

    if (current == NULL || refreshed_out == NULL || current->refresh_token == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    status = agnc_oauth_build_refresh_request(
        current->refresh_token,
        AGNC_OAUTH_ANTHROPIC_CLIENT_ID,
        &request_json);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    snprintf(user_agent, sizeof(user_agent), "User-Agent: agnc/%s", AGNC_VERSION_STRING);
    snprintf(extra_header, sizeof(extra_header), "%s", user_agent);

    for (url_index = 0; AGNC_OAUTH_ANTHROPIC_TOKEN_URLS[url_index] != NULL; url_index++) {
        char *response_body = NULL;
        char *error_message = NULL;
        agnc_status_t attempt;

        attempt = agnc_http_post(
            AGNC_OAUTH_ANTHROPIC_TOKEN_URLS[url_index],
            NULL,
            request_json,
            &response_body,
            &error_message,
            NULL,
            extra_header);

        if (attempt == AGNC_STATUS_OK && response_body != NULL) {
            attempt = agnc_oauth_parse_refresh_response(response_body, current, refreshed_out, now_unix);
            free(response_body);
            free(error_message);
            free(request_json);
            return attempt;
        }

        free(response_body);
        free(error_message);
        status = attempt;
    }

    free(request_json);
    return status;
}

agnc_status_t agnc_oauth_refresh_if_needed(const char *provider_id, int force)
{
    agnc_oauth_token_t current;
    agnc_oauth_token_t refreshed;
    agnc_status_t status;
    long now_unix = (long)time(NULL);

    if (provider_id == NULL || provider_id[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (!agnc_oauth_provider_is_anthropic(provider_id)) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_oauth_token_init(&current);
    status = agnc_oauth_token_load(provider_id, &current);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (!agnc_oauth_token_needs_refresh_at(&current, now_unix, force)) {
        agnc_oauth_token_free(&current);
        return AGNC_STATUS_OK;
    }

    if (current.refresh_token == NULL || current.refresh_token[0] == '\0') {
        agnc_oauth_token_free(&current);
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    agnc_oauth_token_init(&refreshed);
    status = agnc_oauth_refresh_anthropic(&current, &refreshed);
    if (status != AGNC_STATUS_OK) {
        agnc_oauth_token_free(&current);
        agnc_oauth_token_free(&refreshed);
        return status;
    }

    status = agnc_oauth_token_save(provider_id, &refreshed);
    agnc_oauth_token_free(&current);
    agnc_oauth_token_free(&refreshed);
    return status;
}

agnc_status_t agnc_oauth_load_fresh_access_token(const char *provider_id, char **access_out)
{
    agnc_oauth_token_t token;
    agnc_status_t status;

    if (provider_id == NULL || access_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *access_out = NULL;

    status = agnc_oauth_refresh_if_needed(provider_id, 0);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    agnc_oauth_token_init(&token);
    status = agnc_oauth_token_load(provider_id, &token);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    if (token.access_token == NULL || token.access_token[0] == '\0') {
        agnc_oauth_token_free(&token);
        return AGNC_STATUS_IO_ERROR;
    }

    *access_out = token.access_token;
    token.access_token = NULL;
    agnc_oauth_token_free(&token);
    return AGNC_STATUS_OK;
}

int agnc_oauth_token_health(const char *provider_id, char *detail, size_t detail_cap)
{
    agnc_oauth_token_t token;
    long now_unix = (long)time(NULL);
    long seconds_left;

    if (detail != NULL && detail_cap > 0) {
        detail[0] = '\0';
    }

    if (provider_id == NULL) {
        return -1;
    }

    if (agnc_oauth_token_load(provider_id, &token) != AGNC_STATUS_OK) {
        if (detail != NULL && detail_cap > 0) {
            snprintf(detail, detail_cap, "belum ada token");
        }
        return -1;
    }

    if (token.expires_at <= 0) {
        if (detail != NULL && detail_cap > 0) {
            if (token.refresh_token != NULL) {
                snprintf(detail, detail_cap, "token manual (tanpa expires_at)");
            } else {
                snprintf(detail, detail_cap, "token statis (tanpa refresh)");
            }
        }
        agnc_oauth_token_free(&token);
        return 0;
    }

    seconds_left = agnc_oauth_token_seconds_until_expiry_at(&token, now_unix);
    if (seconds_left < 0) {
        if (detail != NULL && detail_cap > 0) {
            snprintf(detail, detail_cap, "kedaluwarsa %ld detik lalu", -seconds_left);
        }
        agnc_oauth_token_free(&token);
        return 2;
    }

    if (seconds_left <= AGNC_OAUTH_ADVISORY_REFRESH_SECONDS) {
        if (detail != NULL && detail_cap > 0) {
            snprintf(detail, detail_cap, "kadaluarsa dalam %ld detik", seconds_left);
        }
        agnc_oauth_token_free(&token);
        return 1;
    }

    if (detail != NULL && detail_cap > 0) {
        snprintf(detail, detail_cap, "valid (%ld detik tersisa)", seconds_left);
    }
    agnc_oauth_token_free(&token);
    return 0;
}

static void agnc_oauth_print_usage(void)
{
    fputs(
        "Usage:\n"
        "  agnc oauth set <provider> [--token TOKEN]\n"
        "  agnc oauth status [provider]\n"
        "  agnc oauth clear <provider>\n"
        "  agnc oauth refresh <provider> [--force]\n",
        stderr);
}

int agnc_cli_run_oauth(int argc, char **argv)
{
    const char *subcommand;
    const char *provider_id;
    agnc_oauth_token_t token;
    char *path = NULL;

    if (argc < 3) {
        agnc_oauth_print_usage();
        return 1;
    }

    subcommand = argv[2];
    provider_id = argc >= 4 ? argv[3] : NULL;

    if (strcmp(subcommand, "set") == 0) {
        const char *token_text = NULL;
        int index;

        if (provider_id == NULL) {
            agnc_oauth_print_usage();
            return 1;
        }

        for (index = 4; index < argc; index++) {
            if (strcmp(argv[index], "--token") == 0 && index + 1 < argc) {
                token_text = argv[index + 1];
                break;
            }
        }

        agnc_oauth_token_init(&token);
        if (token_text != NULL) {
            token.access_token = agnc_strdup_local(token_text);
        } else {
            char buffer[8192];
            size_t length = 0;

            while (length + 1 < sizeof(buffer) && fgets(buffer + length, (int)(sizeof(buffer) - length), stdin) != NULL) {
                length = strlen(buffer);
                if (length > 0 && buffer[length - 1] == '\n') {
                    buffer[length - 1] = '\0';
                    break;
                }
            }
            if (buffer[0] == '\0') {
                fprintf(stderr, "agnc: token kosong (gunakan --token atau pipe stdin)\n");
                return 1;
            }
            token.access_token = agnc_strdup_local(buffer);
        }

        if (token.access_token == NULL) {
            return 1;
        }

        if (agnc_oauth_token_save(provider_id, &token) != AGNC_STATUS_OK) {
            agnc_oauth_token_free(&token);
            fprintf(stderr, "agnc: gagal menyimpan token oauth\n");
            return 1;
        }

        agnc_oauth_token_free(&token);
        printf("oauth token disimpan untuk provider %s\n", provider_id);
        return 0;
    }

    if (strcmp(subcommand, "status") == 0) {
        if (provider_id != NULL) {
            if (agnc_oauth_token_path(provider_id, &path) == AGNC_STATUS_OK) {
                printf("%s: %s\n", provider_id, path);
                free(path);
            }
            if (agnc_oauth_token_load(provider_id, &token) == AGNC_STATUS_OK) {
                long now_unix = (long)time(NULL);
                long seconds_left = agnc_oauth_token_seconds_until_expiry_at(&token, now_unix);

                printf("  access_token: %zu chars\n", strlen(token.access_token));
                if (token.refresh_token != NULL) {
                    printf("  refresh_token: ya\n");
                }
                if (token.expires_at > 0) {
                    printf("  expires_at: %ld\n", token.expires_at);
                    if (seconds_left >= 0) {
                        printf("  expires_in: %ld detik\n", seconds_left);
                    } else {
                        printf("  expires_in: kedaluwarsa %ld detik lalu\n", -seconds_left);
                    }
                }
                agnc_oauth_token_free(&token);
            } else {
                printf("  (belum ada token)\n");
            }
            return 0;
        }

        fputs("agnc oauth status <provider>\n", stderr);
        return 1;
    }

    if (strcmp(subcommand, "refresh") == 0) {
        int force = 0;
        int index;

        if (provider_id == NULL) {
            agnc_oauth_print_usage();
            return 1;
        }

        for (index = 4; index < argc; index++) {
            if (strcmp(argv[index], "--force") == 0) {
                force = 1;
            }
        }

        if (agnc_oauth_refresh_if_needed(provider_id, force) != AGNC_STATUS_OK) {
            fprintf(stderr, "agnc: gagal refresh oauth untuk %s\n", provider_id);
            return 1;
        }

        printf("oauth token %s untuk provider %s\n", force ? "di-refresh (force)" : "diperbarui jika perlu", provider_id);
        return 0;
    }

    if (strcmp(subcommand, "clear") == 0) {
        if (provider_id == NULL) {
            agnc_oauth_print_usage();
            return 1;
        }

        if (agnc_oauth_token_delete(provider_id) != AGNC_STATUS_OK) {
            fprintf(stderr, "agnc: gagal menghapus token (mungkin belum ada)\n");
            return 1;
        }

        printf("oauth token dihapus untuk %s\n", provider_id);
        return 0;
    }

    agnc_oauth_print_usage();
    return 1;
}
