/*
 * conversation.c
 *
 * Manajemen daftar pesan chat untuk agent loop multi-turn.
 */

#include "agnc/conversation.h"

#include <stdlib.h>
#include <string.h>
#include <string.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

void agnc_conversation_init(agnc_conversation_t *conversation)
{
    if (conversation == NULL) {
        return;
    }
    memset(conversation, 0, sizeof(*conversation));
}

void agnc_conversation_clear(agnc_conversation_t *conversation)
{
    size_t index;

    if (conversation == NULL) {
        return;
    }

    for (index = 0; index < conversation->count; index++) {
        agnc_conversation_message_t *item = &conversation->items[index];

        free(item->role);
        free(item->content);
        free(item->tool_call_id);
        free(item->tool_name);
        free(item->tool_arguments);
        item->role = NULL;
        item->content = NULL;
        item->tool_call_id = NULL;
        item->tool_name = NULL;
        item->tool_arguments = NULL;
    }

    conversation->count = 0;
}

agnc_status_t agnc_conversation_push(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments)
{
    agnc_conversation_message_t *message;

    if (conversation == NULL || role == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (conversation->count >= AGNC_MAX_MESSAGES) {
        return AGNC_STATUS_PROVIDER_ERROR;
    }

    message = &conversation->items[conversation->count++];
    message->role = agnc_strdup_local(role);
    message->content = content != NULL ? agnc_strdup_local(content) : NULL;
    message->tool_call_id = tool_call_id != NULL ? agnc_strdup_local(tool_call_id) : NULL;
    message->tool_name = tool_name != NULL ? agnc_strdup_local(tool_name) : NULL;
    message->tool_arguments = tool_arguments != NULL ? agnc_strdup_local(tool_arguments) : NULL;

    if (message->role == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_conversation_ensure_system(agnc_conversation_t *conversation, const char *system_prompt)
{
    size_t index;

    if (conversation == NULL || system_prompt == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0; index < conversation->count; index++) {
        agnc_conversation_message_t *item = &conversation->items[index];

        if (item->role == NULL || strcmp(item->role, "system") != 0) {
            continue;
        }

        if (item->content != NULL && strcmp(item->content, system_prompt) == 0) {
            return AGNC_STATUS_OK;
        }

        free(item->content);
        item->content = agnc_strdup_local(system_prompt);
        if (item->content == NULL) {
            return AGNC_STATUS_OUT_OF_MEMORY;
        }
        return AGNC_STATUS_OK;
    }

    return agnc_conversation_push(conversation, "system", system_prompt, NULL, NULL, NULL);
}

agnc_status_t agnc_conversation_compact_if_needed(
    agnc_conversation_t *conversation,
    size_t threshold,
    size_t keep_tail_messages)
{
    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (conversation->count < threshold) {
        return AGNC_STATUS_OK;
    }

    return agnc_conversation_compact(conversation, keep_tail_messages);
}

agnc_status_t agnc_conversation_compact(agnc_conversation_t *conversation, size_t keep_tail_messages)
{
    agnc_conversation_t kept;
    size_t start;
    size_t index;
    agnc_status_t status = AGNC_STATUS_OK;
    int has_system = 0;

    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (conversation->count <= keep_tail_messages + 1) {
        return AGNC_STATUS_OK;
    }

    agnc_conversation_init(&kept);

    if (conversation->count > 0 && conversation->items[0].role != NULL &&
        strcmp(conversation->items[0].role, "system") == 0) {
        status = agnc_conversation_push(
            &kept,
            "system",
            conversation->items[0].content,
            NULL,
            NULL,
            NULL);
        if (status != AGNC_STATUS_OK) {
            agnc_conversation_clear(&kept);
            return status;
        }
        has_system = 1;
    }

    if (conversation->count <= keep_tail_messages + (size_t)has_system) {
        agnc_conversation_clear(&kept);
        return AGNC_STATUS_OK;
    }

    start = conversation->count - keep_tail_messages;
    for (index = start; index < conversation->count; index++) {
        agnc_conversation_message_t *item = &conversation->items[index];

        status = agnc_conversation_push(
            &kept,
            item->role,
            item->content,
            item->tool_call_id,
            item->tool_name,
            item->tool_arguments);
        if (status != AGNC_STATUS_OK) {
            agnc_conversation_clear(&kept);
            return status;
        }
    }

    agnc_conversation_clear(conversation);
    *conversation = kept;
    return AGNC_STATUS_OK;
}
