/*
 * read_file.c
 *
 * Tool read_file untuk Fase 1 spike.
 * Membaca isi file teks dengan batas ukuran agar output tidak membengkak.
 */

#include "agnc/status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define AGNC_READ_FILE_MAX_BYTES (256 * 1024)

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

/*
 * Fallback jika arguments JSON rusak dari stream model:
 * cari nilai "path" valid di teks mentah (abaikan fragmen seperti "{" saja).
 */
static char *agnc_extract_path_from_jsonish(const char *text)
{
    const char *cursor;
    const char *end;
    size_t length;
    char *path;
    char *best;

    if (text == NULL) {
        return NULL;
    }

    best = NULL;
    cursor = text;
    while ((cursor = strstr(cursor, "\"path\"")) != NULL) {
        cursor += 6;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ':') {
            cursor++;
        }
        if (*cursor != '"') {
            continue;
        }
        cursor++;
        end = cursor;
        while (*end != '\0' && *end != '"') {
            end++;
        }
        if (*end != '"') {
            continue;
        }

        length = (size_t)(end - cursor);
        path = (char *)malloc(length + 1);
        if (path == NULL) {
            free(best);
            return NULL;
        }
        memcpy(path, cursor, length);
        path[length] = '\0';

        if (path[0] == '\0' || strchr(path, '{') != NULL || strchr(path, '\n') != NULL) {
            free(path);
            continue;
        }

        free(best);
        best = path;
        cursor = end + 1;
    }

    return best;
}

/*
 * Buka file relatif dari cwd; jika gagal, coba prefix parent
 * (agnc sering dijalankan dari out/build/x64-Debug).
 */
static FILE *agnc_open_file_with_fallback(const char *path)
{
    FILE *file;
    static const char *prefixes[] = {
        "../../",
        "../../../",
        "../../../../",
        "../../../../../",
        "../../../../../../",
    };
    char fallback[1024];
    size_t index;

    file = fopen(path, "rb");
    if (file != NULL || (path[0] != '\0' && path[1] == ':')) {
        return file;
    }

    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        snprintf(fallback, sizeof(fallback), "%s%s", prefixes[index], path);
        file = fopen(fallback, "rb");
        if (file != NULL) {
            return file;
        }
    }

    {
        const char *workspace = getenv("AGNC_WORKSPACE");
        if (workspace != NULL && workspace[0] != '\0') {
            snprintf(fallback, sizeof(fallback), "%s/%s", workspace, path);
            file = fopen(fallback, "rb");
        }
    }

    return file;
}

agnc_status_t agnc_tool_read_file_execute(const char *arguments_json, char **result_text)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *path_value;
    char *owned_path;
    const char *path;
    FILE *file;
    long file_size;
    char *buffer;
    size_t read_size;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;
    doc = NULL;
    owned_path = NULL;
    path = NULL;

    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc != NULL) {
        root = yyjson_doc_get_root(doc);
        path_value = yyjson_obj_get(root, "path");
        if (path_value != NULL && yyjson_is_str(path_value)) {
            path = yyjson_get_str(path_value);
        }
    }

    if (path == NULL) {
        owned_path = agnc_extract_path_from_jsonish(arguments_json);
        path = owned_path;
    }

    if (path == NULL || path[0] == '\0') {
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: missing path argument");
        return AGNC_STATUS_TOOL_FAILED;
    }

    file = agnc_open_file_with_fallback(path);
    if (file == NULL) {
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: cannot open file");
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: cannot seek file");
        return AGNC_STATUS_TOOL_FAILED;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: cannot determine file size");
        return AGNC_STATUS_TOOL_FAILED;
    }

    if ((size_t)file_size > AGNC_READ_FILE_MAX_BYTES) {
        fclose(file);
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: file too large");
        return AGNC_STATUS_TOOL_FAILED;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        yyjson_doc_free(doc);
        free(owned_path);
        *result_text = agnc_strdup_local("error: cannot rewind file");
        return AGNC_STATUS_TOOL_FAILED;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        yyjson_doc_free(doc);
        free(owned_path);
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    yyjson_doc_free(doc);
    free(owned_path);

    if (read_size != (size_t)file_size) {
        free(buffer);
        *result_text = agnc_strdup_local("error: incomplete file read");
        return AGNC_STATUS_TOOL_FAILED;
    }

    buffer[read_size] = '\0';
    *result_text = buffer;
    return AGNC_STATUS_OK;
}
