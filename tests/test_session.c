/*
 * test_session.c
 *
 * Unit test save/load session SQLite (Fase 4 + 6.8).
 */

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/path.h"
#include "agnc/session.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static char g_session_path[1024];

static int setup_session_path(void **state)
{
    (void)state;

    if (getcwd(g_session_path, sizeof(g_session_path) - 32) == NULL) {
        return -1;
    }

#ifdef _WIN32
    strncat(g_session_path, "\\agnc_session_test.sqlite", sizeof(g_session_path) - strlen(g_session_path) - 1);
#else
    strncat(g_session_path, "/agnc_session_test.sqlite", sizeof(g_session_path) - strlen(g_session_path) - 1);
#endif

    remove(g_session_path);
    {
        char legacy[1100];
        snprintf(legacy, sizeof(legacy), "%.*s", (int)(sizeof(legacy) - 32), g_session_path);
        legacy[strlen(legacy) - strlen(".sqlite")] = '\0';
        strncat(legacy, ".json", sizeof(legacy) - strlen(legacy) - 1);
        remove(legacy);
    }
    return 0;
}

static int teardown_session_path(void **state)
{
    (void)state;
    remove(g_session_path);
    {
        char legacy[1100];
        snprintf(legacy, sizeof(legacy), "%.*s", (int)(sizeof(legacy) - 32), g_session_path);
        legacy[strlen(legacy) - strlen(".sqlite")] = '\0';
        strncat(legacy, ".json", sizeof(legacy) - strlen(legacy) - 1);
        remove(legacy);
    }
    return 0;
}

static void test_session_save_load_roundtrip(void **state)
{
    agnc_conversation_t conversation;
    agnc_conversation_t loaded;
    agnc_config_t config;
    char *provider_id = NULL;
    char *model = NULL;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    agnc_config_init(&config);
#ifdef _MSC_VER
    config.provider_id = _strdup("openrouter");
    config.model = _strdup("test/model");
    config.gateway_id = _strdup("openrouter");
#else
    config.provider_id = strdup("openrouter");
    config.model = strdup("test/model");
    config.gateway_id = strdup("openrouter");
#endif

    assert_int_equal(agnc_conversation_push(&conversation, "system", "sys", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_conversation_push(&conversation, "user", "hello", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_conversation_push(&conversation, "assistant", "hi there", NULL, NULL, NULL), AGNC_STATUS_OK);

    status = agnc_session_save(g_session_path, &conversation, &config);
    assert_int_equal(status, AGNC_STATUS_OK);

    agnc_conversation_init(&loaded);
    status = agnc_session_load(g_session_path, &loaded, &provider_id, &model);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(loaded.count, 3);
    assert_string_equal(loaded.items[1].role, "user");
    assert_string_equal(loaded.items[1].content, "hello");
    assert_string_equal(loaded.items[2].content, "hi there");
    assert_non_null(provider_id);
    assert_string_equal(provider_id, "openrouter");
    assert_non_null(model);
    assert_string_equal(model, "test/model");

    free(provider_id);
    free(model);
    agnc_conversation_clear(&loaded);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
}

static void test_session_cleanup_stale_temp_files(void **state)
{
    char dir[512];
    char stale_path[576];
    FILE *file;

    (void)state;

    if (getcwd(dir, sizeof(dir) - 32) == NULL) {
        return;
    }

#ifdef _WIN32
    snprintf(stale_path, sizeof(stale_path), "%s\\current.json.tmp.99999", dir);
#else
    snprintf(stale_path, sizeof(stale_path), "%s/current.json.tmp.99999", dir);
#endif

    file = fopen(stale_path, "wb");
    assert_non_null(file);
    fclose(file);

    assert_int_equal(agnc_session_cleanup_stale_temp_files_in_dir(dir), AGNC_STATUS_OK);
    assert_int_equal(agnc_path_exists(stale_path), 0);

    remove(stale_path);
}

static void test_conversation_ensure_system_updates(void **state)
{
    agnc_conversation_t conversation;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    assert_int_equal(
        agnc_conversation_ensure_system(&conversation, "prompt v1"), AGNC_STATUS_OK);
    assert_int_equal(conversation.count, 1);
    assert_string_equal(conversation.items[0].content, "prompt v1");

    status = agnc_conversation_ensure_system(&conversation, "prompt v2");
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(conversation.count, 1);
    assert_string_equal(conversation.items[0].content, "prompt v2");

    agnc_conversation_clear(&conversation);
}

static void test_conversation_trim_after_sync(void **state)
{
    agnc_conversation_t conversation;
    size_t index;

    (void)state;

    agnc_conversation_init(&conversation);
    assert_int_equal(agnc_conversation_push(&conversation, "system", "sys", NULL, NULL, NULL), AGNC_STATUS_OK);

    for (index = 0; index < 60; index++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "msg-%zu", index);
        assert_int_equal(agnc_conversation_push(&conversation, "user", buf, NULL, NULL, NULL), AGNC_STATUS_OK);
        conversation.unsynced_count = 0;
        conversation.db_total++;
        conversation.memory_skipped = conversation.db_total - conversation.count;
    }

    assert_true(conversation.count <= AGNC_CONVERSATION_MEMORY_LIMIT);
    assert_true(conversation.memory_skipped > 0);

    agnc_conversation_clear(&conversation);
}

static void test_conversation_compact(void **state)
{
    agnc_conversation_t conversation;
    size_t index;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    assert_int_equal(agnc_conversation_push(&conversation, "system", "sys", NULL, NULL, NULL), AGNC_STATUS_OK);

    for (index = 0; index < 20; index++) {
        char user_buf[32];
        char assistant_buf[32];

        snprintf(user_buf, sizeof(user_buf), "user-%zu", index);
        snprintf(assistant_buf, sizeof(assistant_buf), "asst-%zu", index);
        assert_int_equal(agnc_conversation_push(&conversation, "user", user_buf, NULL, NULL, NULL), AGNC_STATUS_OK);
        assert_int_equal(
            agnc_conversation_push(&conversation, "assistant", assistant_buf, NULL, NULL, NULL), AGNC_STATUS_OK);
    }

    status = agnc_conversation_compact(&conversation, 4);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(conversation.count <= 5);

    agnc_conversation_clear(&conversation);
}

static void test_session_validate_name(void **state)
{
    (void)state;

    assert_int_equal(agnc_session_validate_name("current"), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_validate_name("work-2026"), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_validate_name("a"), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_validate_name(NULL), AGNC_STATUS_INVALID_ARGUMENT);
    assert_int_equal(agnc_session_validate_name(""), AGNC_STATUS_INVALID_ARGUMENT);
    assert_int_equal(agnc_session_validate_name("bad name"), AGNC_STATUS_INVALID_ARGUMENT);
    assert_int_equal(agnc_session_validate_name("bad/name"), AGNC_STATUS_INVALID_ARGUMENT);
}

static void test_session_path_for_name(void **state)
{
    char *path = NULL;

    (void)state;

    assert_int_equal(agnc_session_path_for_name("current", &path), AGNC_STATUS_OK);
    assert_non_null(path);
    assert_true(strstr(path, "current.sqlite") != NULL);
    free(path);

    path = NULL;
    assert_int_equal(agnc_session_path_for_name("my-session", &path), AGNC_STATUS_OK);
    assert_non_null(path);
    assert_true(strstr(path, "my-session.sqlite") != NULL);
    free(path);
}

static void test_session_list_names(void **state)
{
    char dir[512];
    char session_dir[576];
    char path_a[640];
    char path_b[640];
    char **names = NULL;
    size_t count = 0;
    FILE *file;

    (void)state;

    if (getcwd(dir, sizeof(dir) - 64) == NULL) {
        return;
    }

#ifdef _WIN32
    snprintf(session_dir, sizeof(session_dir), "%s\\agnc_session_list_test", dir);
    _mkdir(session_dir);
    snprintf(path_a, sizeof(path_a), "%s\\alpha.sqlite", session_dir);
    snprintf(path_b, sizeof(path_b), "%s\\beta.sqlite", session_dir);
#else
    snprintf(session_dir, sizeof(session_dir), "%s/agnc_session_list_test", dir);
    mkdir(session_dir, 0700);
    snprintf(path_a, sizeof(path_a), "%s/alpha.sqlite", session_dir);
    snprintf(path_b, sizeof(path_b), "%s/beta.sqlite", session_dir);
#endif

    file = fopen(path_a, "wb");
    assert_non_null(file);
    fclose(file);
    file = fopen(path_b, "wb");
    assert_non_null(file);
    fclose(file);

#ifdef _WIN32
    {
        char stale[640];
        snprintf(stale, sizeof(stale), "%s\\gamma.sqlite.tmp.1", session_dir);
        file = fopen(stale, "wb");
        assert_non_null(file);
        fclose(file);
    }
#endif

    assert_int_equal(agnc_session_list_names(session_dir, &names, &count), AGNC_STATUS_OK);
    assert_int_equal((int)count, 2);
    assert_string_equal(names[0], "alpha");
    assert_string_equal(names[1], "beta");

    agnc_session_list_names_free(names, count);
    remove(path_a);
    remove(path_b);
#ifdef _WIN32
    {
        char stale[640];
        snprintf(stale, sizeof(stale), "%s\\gamma.sqlite.tmp.1", session_dir);
        remove(stale);
        _rmdir(session_dir);
    }
#else
    rmdir(session_dir);
#endif
}

static void test_session_migrate_json(void **state)
{
    agnc_conversation_t conversation;
    agnc_conversation_t loaded;
    agnc_config_t config;
    char sqlite_path[1024];
    char json_path[1024];
    FILE *file;
    agnc_status_t status;

    (void)state;

    if (getcwd(sqlite_path, sizeof(sqlite_path) - 64) == NULL) {
        return;
    }

#ifdef _WIN32
    strncat(sqlite_path, "\\agnc_session_migrate.sqlite", sizeof(sqlite_path) - strlen(sqlite_path) - 1);
#else
    strncat(sqlite_path, "/agnc_session_migrate.sqlite", sizeof(sqlite_path) - strlen(sqlite_path) - 1);
#endif

    snprintf(json_path, sizeof(json_path), "%.*s", (int)(sizeof(json_path) - 16), sqlite_path);
    json_path[strlen(json_path) - strlen(".sqlite")] = '\0';
    strncat(json_path, ".json", sizeof(json_path) - strlen(json_path) - 1);

    remove(sqlite_path);
    remove(json_path);

    file = fopen(json_path, "wb");
    assert_non_null(file);
    fprintf(
        file,
        "{"
        "\"provider_id\":\"openrouter\","
        "\"model\":\"test/model\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"legacy\"}]"
        "}");
    fclose(file);

    agnc_conversation_init(&conversation);
    status = agnc_session_load(sqlite_path, &conversation, NULL, NULL);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(conversation.count, 1);
    assert_string_equal(conversation.items[0].content, "legacy");
    assert_int_equal(agnc_path_exists(sqlite_path), 1);
    assert_int_equal(agnc_path_exists(json_path), 0);

    agnc_conversation_init(&loaded);
    agnc_config_init(&config);
    agnc_conversation_clear(&loaded);
    assert_int_equal(agnc_session_clear_messages(sqlite_path, &config), AGNC_STATUS_OK);
    assert_int_equal(agnc_conversation_push(&loaded, "user", "new", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_sync(sqlite_path, &loaded, &config), AGNC_STATUS_OK);

    agnc_conversation_clear(&conversation);
    status = agnc_session_load(sqlite_path, &conversation, NULL, NULL);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(conversation.count, 1);
    assert_string_equal(conversation.items[0].content, "new");

    agnc_conversation_clear(&conversation);
    agnc_conversation_clear(&loaded);
    agnc_config_free(&config);
    remove(sqlite_path);
}

static void test_session_active_name_roundtrip(void **state)
{
    char *original = NULL;
    char *loaded = NULL;

    (void)state;

    assert_int_equal(agnc_session_active_name_load(&original), AGNC_STATUS_OK);
    assert_non_null(original);

    assert_int_equal(agnc_session_active_name_save("roundtrip-test"), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_active_name_load(&loaded), AGNC_STATUS_OK);
    assert_non_null(loaded);
    assert_string_equal(loaded, "roundtrip-test");

    assert_int_equal(agnc_session_active_name_save(original), AGNC_STATUS_OK);

    free(original);
    free(loaded);
}

static void test_session_delete_by_name(void **state)
{
    agnc_conversation_t conversation;
    agnc_config_t config;
    char *path = NULL;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    agnc_config_init(&config);

    assert_int_equal(agnc_session_path_for_name("delete-me-test", &path), AGNC_STATUS_OK);
    assert_non_null(path);

    assert_int_equal(agnc_conversation_push(&conversation, "user", "hello", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_save(path, &conversation, &config), AGNC_STATUS_OK);
    assert_int_equal(agnc_path_exists(path), 1);

    status = agnc_session_delete_by_name("delete-me-test");
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(agnc_path_exists(path), 0);

    status = agnc_session_delete_by_name("delete-me-test");
    assert_int_equal(status, AGNC_STATUS_IO_ERROR);

    assert_int_equal(agnc_session_delete_by_name("bad name"), AGNC_STATUS_INVALID_ARGUMENT);

    free(path);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
}

static void test_session_sync_append(void **state)
{
    agnc_conversation_t conversation;
    agnc_conversation_t loaded;
    agnc_config_t config;
    agnc_status_t status;
    size_t index;

    (void)state;

    agnc_conversation_init(&conversation);
    agnc_config_init(&config);

    assert_int_equal(agnc_conversation_push(&conversation, "user", "one", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);
    assert_int_equal(conversation.unsynced_count, 0);
    assert_int_equal((int)conversation.db_total, 1);

    assert_int_equal(agnc_conversation_push(&conversation, "assistant", "two", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);
    assert_int_equal((int)conversation.db_total, 2);

    for (index = 0; index < 55; index++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "bulk-%zu", index);
        assert_int_equal(agnc_conversation_push(&conversation, "user", buf, NULL, NULL, NULL), AGNC_STATUS_OK);
        if ((index % 16) == 15) {
            assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);
        }
    }
    assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);
    assert_true(conversation.db_total >= 57);

    agnc_conversation_init(&loaded);
    status = agnc_session_load(g_session_path, &loaded, NULL, NULL);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(loaded.count <= AGNC_CONVERSATION_MEMORY_LIMIT);
    assert_true(loaded.memory_skipped > 0);
    assert_true(loaded.db_total >= 57);

    agnc_conversation_clear(&loaded);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
    remove(g_session_path);
}

static void test_session_usage_accumulate(void **state)
{
    agnc_session_usage_t usage;
    agnc_status_t status;

    (void)state;

    remove(g_session_path);
    agnc_session_usage_init(&usage);

    status = agnc_session_usage_accumulate(g_session_path, 100, 50, 150);
    assert_int_equal(status, AGNC_STATUS_OK);

    status = agnc_session_usage_load(g_session_path, &usage);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(usage.prompt_tokens, 100);
    assert_int_equal(usage.completion_tokens, 50);
    assert_int_equal(usage.total_tokens, 150);

    status = agnc_session_usage_accumulate(g_session_path, 20, 30, -1);
    assert_int_equal(status, AGNC_STATUS_OK);

    status = agnc_session_usage_load(g_session_path, &usage);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(usage.prompt_tokens, 120);
    assert_int_equal(usage.completion_tokens, 80);
    assert_int_equal(usage.total_tokens, 200);

    status = agnc_session_usage_reset(g_session_path);
    assert_int_equal(status, AGNC_STATUS_OK);
    status = agnc_session_usage_load(g_session_path, &usage);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal(usage.prompt_tokens, 0);
    assert_int_equal(usage.completion_tokens, 0);
    assert_int_equal(usage.total_tokens, 0);
}

static void test_session_bg_job_foreground_split(void **state)
{
    agnc_conversation_t conversation;
    agnc_conversation_t loaded;
    agnc_config_t config;
    int64_t anchor_id = 0;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    agnc_conversation_init(&loaded);
    agnc_config_init(&config);

    assert_int_equal(agnc_conversation_push(&conversation, "user", "hello", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_conversation_push(&conversation, "assistant", "hi", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);

    status = agnc_session_foreground_max_id(g_session_path, &anchor_id);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(anchor_id >= 2);

    assert_int_equal(agnc_session_bg_job_create(g_session_path, 1, "ringkasan docs", anchor_id), AGNC_STATUS_OK);

    agnc_conversation_clear(&conversation);
    status = agnc_session_load_bg_context(g_session_path, &conversation, anchor_id, AGNC_CONVERSATION_MEMORY_LIMIT);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_int_equal((int)conversation.count, 2);

    assert_int_equal(agnc_conversation_push(&conversation, "user", "ringkasan docs", NULL, NULL, NULL), AGNC_STATUS_OK);
    assert_int_equal(agnc_conversation_push(&conversation, "assistant", "hasil bg", NULL, NULL, NULL), AGNC_STATUS_OK);
    agnc_conversation_mark_unsynced_bg(&conversation, 1);
    assert_int_equal(agnc_session_sync(g_session_path, &conversation, &config), AGNC_STATUS_OK);

    assert_int_equal(
        agnc_session_bg_append_foreground_notice(g_session_path, 1, "ringkasan docs", "hasil bg", &config),
        AGNC_STATUS_OK);
    assert_int_equal(agnc_session_bg_job_set_status(g_session_path, 1, "done", "hasil bg", NULL), AGNC_STATUS_OK);

    status = agnc_session_load(g_session_path, &loaded, NULL, NULL);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(loaded.count >= 4);
    assert_true(loaded.db_total >= 4);

    agnc_conversation_clear(&loaded);
    agnc_conversation_clear(&conversation);
    agnc_config_free(&config);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_session_save_load_roundtrip, setup_session_path, teardown_session_path),
        cmocka_unit_test(test_session_cleanup_stale_temp_files),
        cmocka_unit_test(test_conversation_ensure_system_updates),
        cmocka_unit_test(test_conversation_trim_after_sync),
        cmocka_unit_test(test_conversation_compact),
        cmocka_unit_test(test_session_validate_name),
        cmocka_unit_test(test_session_path_for_name),
        cmocka_unit_test(test_session_list_names),
        cmocka_unit_test(test_session_active_name_roundtrip),
        cmocka_unit_test(test_session_delete_by_name),
        cmocka_unit_test(test_session_migrate_json),
        cmocka_unit_test_setup_teardown(test_session_sync_append, setup_session_path, teardown_session_path),
        cmocka_unit_test_setup_teardown(test_session_bg_job_foreground_split, setup_session_path, teardown_session_path),
        cmocka_unit_test_setup_teardown(test_session_usage_accumulate, setup_session_path, teardown_session_path),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
