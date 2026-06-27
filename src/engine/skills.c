/*
 * skills.c
 *
 * Loader skills markdown: ~/.agnc/skills dan .agnc/skills di workspace.
 */

#include "agnc/skills.h"

#include "agnc/path.h"
#include "agnc/tool_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define AGNC_PATH_SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#define AGNC_PATH_SEP '/'
#endif

#define AGNC_SKILLS_MAX_FILES 16
#define AGNC_SKILLS_MAX_FILE_BYTES (8 * 1024)
#define AGNC_SKILLS_MAX_TOTAL_BYTES (16 * 1024)

typedef struct {
    char *path;
    char *name;
    size_t size_bytes;
} agnc_skill_file_t;

static char *g_skills_cached_context = NULL;
static int g_skills_cache_valid = 0;

static char *agnc_skills_strdup(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_skills_invalidate(void)
{
    free(g_skills_cached_context);
    g_skills_cached_context = NULL;
    g_skills_cache_valid = 0;
}

static int agnc_skills_ends_with(const char *text, const char *suffix)
{
    size_t text_len;
    size_t suffix_len;

    if (text == NULL || suffix == NULL) {
        return 0;
    }

    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }

    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static int agnc_skills_path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

#ifdef _WIN32
    if (path[1] == ':') {
        return 1;
    }
#endif

    return path[0] == '/' || path[0] == '\\';
}

static agnc_status_t agnc_skills_resolve_dir(const char *path, char **resolved_out)
{
    char *expanded = NULL;
    char *workspace = NULL;
    char combined[4096];
    agnc_status_t status;

    if (path == NULL || resolved_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *resolved_out = NULL;
    status = agnc_path_expand_user(path, &expanded);
    if (status != AGNC_STATUS_OK || expanded == NULL) {
        free(expanded);
        return status != AGNC_STATUS_OK ? status : AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (!agnc_skills_path_is_absolute(expanded)) {
        if (agnc_tool_path_workspace_root(&workspace) == AGNC_STATUS_OK && workspace != NULL) {
            snprintf(combined, sizeof(combined), "%s%c%s", workspace, AGNC_PATH_SEP, expanded);
            free(expanded);
            expanded = agnc_skills_strdup(combined);
            if (expanded == NULL) {
                free(workspace);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
        }
        free(workspace);
    }

    *resolved_out = expanded;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_skills_add_search_dir(char ***dirs, size_t *count, size_t *capacity, const char *path)
{
    char *resolved = NULL;
    char **grown;
    agnc_status_t status;
    size_t index;

    status = agnc_skills_resolve_dir(path, &resolved);
    if (status != AGNC_STATUS_OK || resolved == NULL) {
        free(resolved);
        return status;
    }

    for (index = 0; index < *count; index++) {
        if ((*dirs)[index] != NULL && strcmp((*dirs)[index], resolved) == 0) {
            free(resolved);
            return AGNC_STATUS_OK;
        }
    }

    if (*count >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
        grown = (char **)realloc(*dirs, new_capacity * sizeof(*grown));
        if (grown == NULL) {
            free(resolved);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        *dirs = grown;
        *capacity = new_capacity;
    }

    (*dirs)[*count] = resolved;
    (*count)++;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_skills_collect_search_dirs(const agnc_config_t *config, char ***dirs_out, size_t *count_out)
{
    char **dirs = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t index;
    agnc_status_t status = AGNC_STATUS_OK;

    if (dirs_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *dirs_out = NULL;
    *count_out = 0;

    if (config != NULL && config->skills_path_count > 0) {
        for (index = 0; index < config->skills_path_count; index++) {
            status = agnc_skills_add_search_dir(&dirs, &count, &capacity, config->skills_paths[index]);
            if (status != AGNC_STATUS_OK) {
                break;
            }
        }
    } else {
        status = agnc_skills_add_search_dir(&dirs, &count, &capacity, "~/.agnc/skills");
        if (status == AGNC_STATUS_OK) {
            status = agnc_skills_add_search_dir(&dirs, &count, &capacity, ".agnc/skills");
        }
    }

    if (status != AGNC_STATUS_OK) {
        for (index = 0; index < count; index++) {
            free(dirs[index]);
        }
        free(dirs);
        return status;
    }

    *dirs_out = dirs;
    *count_out = count;
    return AGNC_STATUS_OK;
}

static void agnc_skills_free_files(agnc_skill_file_t *files, size_t count)
{
    size_t index;

    if (files == NULL) {
        return;
    }

    for (index = 0; index < count; index++) {
        free(files[index].path);
        free(files[index].name);
    }
}

#ifdef _WIN32
static int agnc_skills_is_directory(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static agnc_status_t agnc_skills_scan_dir(
    const char *dir_path,
    agnc_skill_file_t *files,
    size_t *file_count,
    size_t max_files)
{
    char pattern[4096];
    WIN32_FIND_DATAA entry;
    HANDLE handle;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    handle = FindFirstFileA(pattern, &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        return AGNC_STATUS_OK;
    }

    do {
        char full_path[4096];

        if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, entry.cFileName);

        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char skill_md[4096];
            FILE *file;
            long size;

            snprintf(skill_md, sizeof(skill_md), "%s\\SKILL.md", full_path);
            file = fopen(skill_md, "rb");
            if (file == NULL || *file_count >= max_files) {
                if (file != NULL) {
                    fclose(file);
                }
                continue;
            }

            if (fseek(file, 0, SEEK_END) != 0) {
                fclose(file);
                continue;
            }
            size = ftell(file);
            fclose(file);
            if (size <= 0 || (size_t)size > AGNC_SKILLS_MAX_FILE_BYTES) {
                continue;
            }

            files[*file_count].path = agnc_skills_strdup(skill_md);
            files[*file_count].name = agnc_skills_strdup(entry.cFileName);
            files[*file_count].size_bytes = (size_t)size;
            if (files[*file_count].path == NULL || files[*file_count].name == NULL) {
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
            (*file_count)++;
            continue;
        }

        if (!agnc_skills_ends_with(entry.cFileName, ".md")) {
            continue;
        }
        if (*file_count >= max_files) {
            continue;
        }

        {
            FILE *file = fopen(full_path, "rb");
            long size;

            if (file == NULL) {
                continue;
            }
            if (fseek(file, 0, SEEK_END) != 0) {
                fclose(file);
                continue;
            }
            size = ftell(file);
            fclose(file);
            if (size <= 0 || (size_t)size > AGNC_SKILLS_MAX_FILE_BYTES) {
                continue;
            }

            files[*file_count].path = agnc_skills_strdup(full_path);
            files[*file_count].name = agnc_skills_strdup(entry.cFileName);
            files[*file_count].size_bytes = (size_t)size;
            if (files[*file_count].path == NULL || files[*file_count].name == NULL) {
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
            (*file_count)++;
        }
    } while (FindNextFileA(handle, &entry));

    FindClose(handle);
    return AGNC_STATUS_OK;
}
#else
static agnc_status_t agnc_skills_scan_dir(
    const char *dir_path,
    agnc_skill_file_t *files,
    size_t *file_count,
    size_t max_files)
{
    DIR *dir = opendir(dir_path);

    if (dir == NULL) {
        return AGNC_STATUS_OK;
    }

    while (*file_count < max_files) {
        struct dirent *entry = readdir(dir);
        char full_path[4096];
        struct stat stat_buf;

        if (entry == NULL) {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &stat_buf) != 0) {
            continue;
        }

        if (S_ISDIR(stat_buf.st_mode)) {
            char skill_md[4096];
            FILE *file;
            long size;

            snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", full_path);
            file = fopen(skill_md, "rb");
            if (file == NULL) {
                continue;
            }
            if (fseek(file, 0, SEEK_END) != 0) {
                fclose(file);
                continue;
            }
            size = ftell(file);
            fclose(file);
            if (size <= 0 || (size_t)size > AGNC_SKILLS_MAX_FILE_BYTES) {
                continue;
            }

            files[*file_count].path = agnc_skills_strdup(skill_md);
            files[*file_count].name = agnc_skills_strdup(entry->d_name);
            files[*file_count].size_bytes = (size_t)size;
            if (files[*file_count].path == NULL || files[*file_count].name == NULL) {
                closedir(dir);
                return AGNC_STATUS_OUT_OF_MEMORY;
            }
            (*file_count)++;
            continue;
        }

        if (!S_ISREG(stat_buf.st_mode) || !agnc_skills_ends_with(entry->d_name, ".md")) {
            continue;
        }

        if (stat_buf.st_size <= 0 || (size_t)stat_buf.st_size > AGNC_SKILLS_MAX_FILE_BYTES) {
            continue;
        }

        files[*file_count].path = agnc_skills_strdup(full_path);
        files[*file_count].name = agnc_skills_strdup(entry->d_name);
        files[*file_count].size_bytes = (size_t)stat_buf.st_size;
        if (files[*file_count].path == NULL || files[*file_count].name == NULL) {
            closedir(dir);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        (*file_count)++;
    }

    closedir(dir);
    return AGNC_STATUS_OK;
}
#endif

static agnc_status_t agnc_skills_collect_files(const agnc_config_t *config, agnc_skill_file_t *files, size_t *file_count)
{
    char **dirs = NULL;
    size_t dir_count = 0;
    size_t dir_index;
    agnc_status_t status;

    *file_count = 0;
    status = agnc_skills_collect_search_dirs(config, &dirs, &dir_count);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    for (dir_index = 0; dir_index < dir_count && *file_count < AGNC_SKILLS_MAX_FILES; dir_index++) {
#ifdef _WIN32
        if (!agnc_skills_is_directory(dirs[dir_index])) {
            continue;
        }
#endif
        status = agnc_skills_scan_dir(dirs[dir_index], files, file_count, AGNC_SKILLS_MAX_FILES);
        if (status != AGNC_STATUS_OK) {
            break;
        }
    }

    for (dir_index = 0; dir_index < dir_count; dir_index++) {
        free(dirs[dir_index]);
    }
    free(dirs);
    return status;
}

static agnc_status_t agnc_skills_read_file_text(const char *path, size_t max_bytes, char **text_out, size_t *size_out)
{
    FILE *file;
    long file_size;
    char *buffer;
    size_t read_size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    file_size = ftell(file);
    if (file_size <= 0 || (size_t)file_size > max_bytes) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(buffer);
        return AGNC_STATUS_IO_ERROR;
    }

    buffer[read_size] = '\0';
    *text_out = buffer;
    *size_out = read_size;
    return AGNC_STATUS_OK;
}

static agnc_status_t agnc_skills_build_context_fresh(const agnc_config_t *config, char **context_out)
{
    agnc_skill_file_t files[AGNC_SKILLS_MAX_FILES];
    size_t file_count = 0;
    size_t index;
    size_t total = 0;
    char *output = NULL;
    size_t offset = 0;
    int truncated = 0;
    agnc_status_t status;

    if (context_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *context_out = NULL;
    memset(files, 0, sizeof(files));

    if (config == NULL || !config->skills_enabled) {
        return AGNC_STATUS_OK;
    }

    status = agnc_skills_collect_files(config, files, &file_count);
    if (status != AGNC_STATUS_OK) {
        agnc_skills_free_files(files, file_count);
        return status;
    }

    if (file_count == 0) {
        agnc_skills_free_files(files, file_count);
        return AGNC_STATUS_OK;
    }

    output = (char *)malloc(AGNC_SKILLS_MAX_TOTAL_BYTES + 256);
    if (output == NULL) {
        agnc_skills_free_files(files, file_count);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    offset += (size_t)snprintf(
        output,
        AGNC_SKILLS_MAX_TOTAL_BYTES + 256,
        "Project skills (follow these instructions when relevant): ");

    for (index = 0; index < file_count; index++) {
        char *text = NULL;
        size_t text_size = 0;
        size_t header_len;
        size_t copy_len;

        status = agnc_skills_read_file_text(
            files[index].path,
            AGNC_SKILLS_MAX_FILE_BYTES,
            &text,
            &text_size);
        if (status != AGNC_STATUS_OK) {
            continue;
        }

        header_len = (size_t)snprintf(
            NULL,
            0,
            "\n\n--- skill:%s ---\n",
            files[index].name != NULL ? files[index].name : "unknown");
        if (offset + header_len + text_size + 32 > AGNC_SKILLS_MAX_TOTAL_BYTES) {
            truncated = 1;
            free(text);
            break;
        }

        offset += (size_t)snprintf(
            output + offset,
            AGNC_SKILLS_MAX_TOTAL_BYTES + 256 - offset,
            "\n\n--- skill:%s ---\n",
            files[index].name != NULL ? files[index].name : "unknown");

        copy_len = text_size;
        if (offset + copy_len > AGNC_SKILLS_MAX_TOTAL_BYTES) {
            copy_len = AGNC_SKILLS_MAX_TOTAL_BYTES - offset;
            truncated = 1;
        }

        memcpy(output + offset, text, copy_len);
        offset += copy_len;
        total += copy_len;
        free(text);

        if (truncated) {
            break;
        }
    }

    if (truncated) {
        static const char suffix[] = "\n...(skills truncated)";
        size_t suffix_len = sizeof(suffix) - 1;
        if (offset + suffix_len < AGNC_SKILLS_MAX_TOTAL_BYTES + 256) {
            memcpy(output + offset, suffix, suffix_len + 1);
            offset += suffix_len;
        }
    } else {
        output[offset] = '\0';
    }

    agnc_skills_free_files(files, file_count);

    if (total == 0) {
        free(output);
        return AGNC_STATUS_OK;
    }

    *context_out = output;
    (void)total;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_skills_build_context(const agnc_config_t *config, char **context_out)
{
    if (context_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *context_out = NULL;
    if (config == NULL || !config->skills_enabled) {
        return AGNC_STATUS_OK;
    }

    if (g_skills_cache_valid && g_skills_cached_context != NULL) {
        *context_out = agnc_skills_strdup(g_skills_cached_context);
        return *context_out != NULL ? AGNC_STATUS_OK : AGNC_STATUS_OUT_OF_MEMORY;
    }

    {
        agnc_status_t status = agnc_skills_build_context_fresh(config, context_out);
        if (status != AGNC_STATUS_OK) {
            return status;
        }

        free(g_skills_cached_context);
        g_skills_cached_context = *context_out != NULL ? agnc_skills_strdup(*context_out) : NULL;
        g_skills_cache_valid = 1;
        return AGNC_STATUS_OK;
    }
}

agnc_status_t agnc_skills_list(const agnc_config_t *config, agnc_skill_entry_t **entries_out, size_t *count_out)
{
    agnc_skill_file_t files[AGNC_SKILLS_MAX_FILES];
    size_t file_count = 0;
    agnc_skill_entry_t *entries = NULL;
    size_t index;
    agnc_status_t status;

    if (entries_out == NULL || count_out == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *entries_out = NULL;
    *count_out = 0;

    if (config == NULL || !config->skills_enabled) {
        return AGNC_STATUS_OK;
    }

    memset(files, 0, sizeof(files));
    status = agnc_skills_collect_files(config, files, &file_count);
    if (status != AGNC_STATUS_OK) {
        agnc_skills_free_files(files, file_count);
        return status;
    }

    if (file_count == 0) {
        return AGNC_STATUS_OK;
    }

    entries = (agnc_skill_entry_t *)calloc(file_count, sizeof(*entries));
    if (entries == NULL) {
        agnc_skills_free_files(files, file_count);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    for (index = 0; index < file_count; index++) {
        entries[index].name = agnc_skills_strdup(files[index].name);
        entries[index].path = agnc_skills_strdup(files[index].path);
        entries[index].size_bytes = files[index].size_bytes;
        if (entries[index].name == NULL || entries[index].path == NULL) {
            agnc_skills_list_free(entries, file_count);
            agnc_skills_free_files(files, file_count);
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
    }

    agnc_skills_free_files(files, file_count);
    *entries_out = entries;
    *count_out = file_count;
    return AGNC_STATUS_OK;
}

void agnc_skills_list_free(agnc_skill_entry_t *entries, size_t count)
{
    size_t index;

    if (entries == NULL) {
        return;
    }

    for (index = 0; index < count; index++) {
        free(entries[index].name);
        free(entries[index].path);
    }
    free(entries);
}
