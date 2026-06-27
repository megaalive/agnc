/*
 * todo_write.c
 *
 * Tool todo_write: simpan daftar todo agent ke ~/.agnc/todos.json.
 */

#include "agnc/tool.h"
#include "agnc/path.h"
#include "agnc/atomic_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_todo_default_path(char **path_out)
{
    char *expanded = NULL;
    agnc_status_t status;

    status = agnc_path_expand_user("~/.agnc/todos.json", &expanded);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    *path_out = expanded;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_tool_todo_write_execute(const char *arguments_json, char **result_text)
{
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *todos_value;
    char *path = NULL;
    char *output = NULL;
    agnc_status_t status;

    if (arguments_json == NULL || result_text == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *result_text = NULL;
    doc = yyjson_read(arguments_json, strlen(arguments_json), 0);
    if (doc == NULL) {
        *result_text = agnc_strdup_local("error: invalid JSON arguments");
        return AGNC_STATUS_TOOL_FAILED;
    }

    root = yyjson_doc_get_root(doc);
    todos_value = yyjson_obj_get(root, "todos");
    if (todos_value == NULL || !yyjson_is_arr(todos_value)) {
        yyjson_doc_free(doc);
        *result_text = agnc_strdup_local("error: missing todos array");
        return AGNC_STATUS_TOOL_FAILED;
    }

    output = yyjson_val_write(todos_value, YYJSON_WRITE_PRETTY, NULL);
    yyjson_doc_free(doc);
    if (output == NULL) {
        *result_text = agnc_strdup_local("error: out of memory");
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    status = agnc_todo_default_path(&path);
    if (status != AGNC_STATUS_OK) {
        free(output);
        *result_text = agnc_strdup_local("error: cannot resolve todo path");
        return AGNC_STATUS_TOOL_FAILED;
    }

    status = agnc_atomic_write_file(path, output, strlen(output));
    free(path);
    free(output);

    if (status != AGNC_STATUS_OK) {
        *result_text = agnc_strdup_local("error: failed to write todos.json");
        return AGNC_STATUS_TOOL_FAILED;
    }

    *result_text = agnc_strdup_local("ok: todos saved to ~/.agnc/todos.json");
    return AGNC_STATUS_OK;
}
