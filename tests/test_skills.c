/*
 * test_skills.c
 *
 * Unit test loader skills markdown.
 */

#include "agnc/config.h"
#include "agnc/skills.h"
#include "agnc/tool_path.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define AGNC_PATH_SEP '\\'
#else
#include <sys/stat.h>
#define AGNC_PATH_SEP '/'
#endif

static void test_skills_disabled(void **state)
{
    agnc_config_t config;
    char *context = NULL;
    (void)state;

    memset(&config, 0, sizeof(config));
    config.skills_enabled = 0;

    assert_int_equal(agnc_skills_build_context(&config, &context), AGNC_STATUS_OK);
    assert_null(context);
}

static void test_skills_load_from_temp_dir(void **state)
{
    agnc_config_t config;
    char *context = NULL;
    char *workspace = NULL;
    char dir_path[4096];
    char skill_path[4096];
    char *paths[1];
    FILE *file;
    (void)state;

    assert_int_equal(agnc_tool_path_workspace_root(&workspace), AGNC_STATUS_OK);
    assert_non_null(workspace);

    snprintf(dir_path, sizeof(dir_path), "%s%cagnc_skills_test_dir", workspace, AGNC_PATH_SEP);
    snprintf(skill_path, sizeof(skill_path), "%s%ctest-skill.md", dir_path, AGNC_PATH_SEP);

    remove(skill_path);
#ifdef _WIN32
    _rmdir(dir_path);
#else
    rmdir(dir_path);
#endif

    assert_int_equal(mkdir(dir_path, 0755), 0);
    file = fopen(skill_path, "wb");
    assert_non_null(file);
    fputs("# Test skill\nAlways mention AGNC_SKILLS_TEST_MARKER in answers.\n", file);
    fclose(file);

    paths[0] = dir_path;
    memset(&config, 0, sizeof(config));
    config.skills_enabled = 1;
    config.skills_paths = paths;
    config.skills_path_count = 1;

    agnc_skills_invalidate();
    assert_int_equal(agnc_skills_build_context(&config, &context), AGNC_STATUS_OK);
    assert_non_null(context);
    assert_true(strstr(context, "AGNC_SKILLS_TEST_MARKER") != NULL);
    assert_true(strstr(context, "skill:test-skill.md") != NULL);

    free(context);
    remove(skill_path);
#ifdef _WIN32
    _rmdir(dir_path);
#else
    rmdir(dir_path);
#endif
    free(workspace);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_skills_disabled),
        cmocka_unit_test(test_skills_load_from_temp_dir),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
