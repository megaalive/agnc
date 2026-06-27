/*
 * test_session.c
 *
 * Unit test save/load session JSON (Fase 4).
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
    strncat(g_session_path, "\\agnc_session_test.json", sizeof(g_session_path) - strlen(g_session_path) - 1);
#else
    strncat(g_session_path, "/agnc_session_test.json", sizeof(g_session_path) - strlen(g_session_path) - 1);
#endif

    remove(g_session_path);
    return 0;
}

static int teardown_session_path(void **state)
{
    (void)state;
    remove(g_session_path);
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

static void test_conversation_compact_when_near_limit(void **state)
{
    agnc_conversation_t conversation;
    size_t index;
    agnc_status_t status;

    (void)state;

    agnc_conversation_init(&conversation);
    assert_int_equal(agnc_conversation_push(&conversation, "system", "sys", NULL, NULL, NULL), AGNC_STATUS_OK);

    for (index = 0; index < AGNC_MAX_MESSAGES - 1; index++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "msg-%zu", index);
        assert_int_equal(agnc_conversation_push(&conversation, "user", buf, NULL, NULL, NULL), AGNC_STATUS_OK);
    }

    assert_int_equal((int)conversation.count, AGNC_MAX_MESSAGES);

    status = agnc_conversation_compact_if_needed(
        &conversation, AGNC_CONVERSATION_COMPACT_THRESHOLD, AGNC_CONVERSATION_COMPACT_KEEP);
    assert_int_equal(status, AGNC_STATUS_OK);
    assert_true(conversation.count < AGNC_MAX_MESSAGES);

    status = agnc_conversation_push(&conversation, "user", "after-compact", NULL, NULL, NULL);
    assert_int_equal(status, AGNC_STATUS_OK);

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
    assert_true(strstr(path, "current.json") != NULL);
    free(path);

    path = NULL;
    assert_int_equal(agnc_session_path_for_name("my-session", &path), AGNC_STATUS_OK);
    assert_non_null(path);
    assert_true(strstr(path, "my-session.json") != NULL);
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
    snprintf(path_a, sizeof(path_a), "%s\\alpha.json", session_dir);
    snprintf(path_b, sizeof(path_b), "%s\\beta.json", session_dir);
#else
    snprintf(session_dir, sizeof(session_dir), "%s/agnc_session_list_test", dir);
    mkdir(session_dir, 0700);
    snprintf(path_a, sizeof(path_a), "%s/alpha.json", session_dir);
    snprintf(path_b, sizeof(path_b), "%s/beta.json", session_dir);
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
        snprintf(stale, sizeof(stale), "%s\\gamma.json.tmp.1", session_dir);
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
        snprintf(stale, sizeof(stale), "%s\\gamma.json.tmp.1", session_dir);
        remove(stale);
        _rmdir(session_dir);
    }
#else
    rmdir(session_dir);
#endif
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_session_save_load_roundtrip, setup_session_path, teardown_session_path),
        cmocka_unit_test(test_session_cleanup_stale_temp_files),
        cmocka_unit_test(test_conversation_ensure_system_updates),
        cmocka_unit_test(test_conversation_compact_when_near_limit),
        cmocka_unit_test(test_conversation_compact),
        cmocka_unit_test(test_session_validate_name),
        cmocka_unit_test(test_session_path_for_name),
        cmocka_unit_test(test_session_list_names),
        cmocka_unit_test(test_session_active_name_roundtrip),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
