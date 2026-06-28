/*
 * conversation.c
 *
 * Manajemen daftar pesan chat dinamis untuk agent loop multi-turn.
 */

#include "agnc/conversation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static agnc_status_t agnc_conversation_reserve(agnc_conversation_t *conversation, size_t needed)
{
    agnc_conversation_message_t *grown;
    size_t new_capacity;

    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    if (needed <= conversation->capacity) {
        return AGNC_STATUS_OK;
    }

    new_capacity = conversation->capacity == 0 ? AGNC_CONVERSATION_INITIAL_CAPACITY : conversation->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    grown = (agnc_conversation_message_t *)realloc(conversation->items, new_capacity * sizeof(*conversation->items));
    if (grown == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (conversation->capacity == 0) {
        memset(grown, 0, new_capacity * sizeof(*grown));
    } else if (new_capacity > conversation->capacity) {
        memset(grown + conversation->capacity, 0, (new_capacity - conversation->capacity) * sizeof(*grown));
    }

    conversation->items = grown;
    conversation->capacity = new_capacity;
    return AGNC_STATUS_OK;
}

static void agnc_conversation_free_message(agnc_conversation_message_t *message)
{
    if (message == NULL) {
        return;
    }

    free(message->role);
    free(message->content);
    free(message->tool_call_id);
    free(message->tool_name);
    free(message->tool_arguments);
    free(message->provider_id);
    free(message->gateway_id);
    free(message->model);
    memset(message, 0, sizeof(*message));
}

static void agnc_conversation_remove_at(agnc_conversation_t *conversation, size_t remove_index)
{
    size_t unsynced_start;

    if (conversation == NULL || remove_index >= conversation->count) {
        return;
    }

    unsynced_start = conversation->count - conversation->unsynced_count;
    if (remove_index >= unsynced_start && conversation->unsynced_count > 0) {
        conversation->unsynced_count--;
    } else {
        conversation->memory_skipped++;
    }

    agnc_conversation_free_message(&conversation->items[remove_index]);
    if (remove_index + 1 < conversation->count) {
        memmove(
            &conversation->items[remove_index],
            &conversation->items[remove_index + 1],
            (conversation->count - remove_index - 1) * sizeof(conversation->items[0]));
    }

    conversation->count--;
    if (conversation->count < conversation->capacity) {
        memset(&conversation->items[conversation->count], 0, sizeof(conversation->items[0]));
    }
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
        agnc_conversation_free_message(&conversation->items[index]);
    }

    free(conversation->items);
    free(conversation->history_summary);
    memset(conversation, 0, sizeof(*conversation));
}

const agnc_conversation_message_t *agnc_conversation_at(const agnc_conversation_t *conversation, size_t index)
{
    if (conversation == NULL || index >= conversation->count) {
        return NULL;
    }

    return &conversation->items[index];
}

agnc_status_t agnc_conversation_push(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments)
{
    agnc_status_t status;

    status = agnc_conversation_push_hydrated(
        conversation, role, content, tool_call_id, tool_name, tool_arguments);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    conversation->unsynced_count++;
    return agnc_conversation_trim_memory(conversation);
}

agnc_status_t agnc_conversation_push_hydrated(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments)
{
    return agnc_conversation_push_hydrated_routing(
        conversation,
        role,
        content,
        tool_call_id,
        tool_name,
        tool_arguments,
        NULL,
        NULL,
        NULL);
}

agnc_status_t agnc_conversation_push_hydrated_routing(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments,
    const char *provider_id,
    const char *gateway_id,
    const char *model)
{
    agnc_conversation_message_t *message;
    agnc_status_t status;

    if (conversation == NULL || role == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    status = agnc_conversation_reserve(conversation, conversation->count + 1);
    if (status != AGNC_STATUS_OK) {
        return status;
    }

    message = &conversation->items[conversation->count++];
    message->role = agnc_strdup_local(role);
    message->content = content != NULL ? agnc_strdup_local(content) : NULL;
    message->tool_call_id = tool_call_id != NULL ? agnc_strdup_local(tool_call_id) : NULL;
    message->tool_name = tool_name != NULL ? agnc_strdup_local(tool_name) : NULL;
    message->tool_arguments = tool_arguments != NULL ? agnc_strdup_local(tool_arguments) : NULL;
    message->parent_id = 0;
    message->is_bg = 0;
    message->job_id = 0;
    message->provider_id = provider_id != NULL ? agnc_strdup_local(provider_id) : NULL;
    message->gateway_id = gateway_id != NULL ? agnc_strdup_local(gateway_id) : NULL;
    message->model = model != NULL ? agnc_strdup_local(model) : NULL;

    if (message->role == NULL) {
        conversation->count--;
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    return AGNC_STATUS_OK;
}

void agnc_conversation_apply_config_routing_to_last(
    agnc_conversation_t *conversation,
    const agnc_config_t *config)
{
    agnc_conversation_message_t *message;

    if (conversation == NULL || conversation->count == 0 || config == NULL) {
        return;
    }

    message = &conversation->items[conversation->count - 1];
    free(message->provider_id);
    free(message->gateway_id);
    free(message->model);
    message->provider_id =
        config->provider_id != NULL ? agnc_strdup_local(config->provider_id) : NULL;
    message->gateway_id = config->gateway_id != NULL ? agnc_strdup_local(config->gateway_id) : NULL;
    message->model = config->model != NULL ? agnc_strdup_local(config->model) : NULL;
}

size_t agnc_conversation_format_routing_label(
    const agnc_conversation_message_t *message,
    char *out,
    size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return 0;
    }

    out[0] = '\0';
    if (message == NULL) {
        return 0;
    }

    if (message->model != NULL && message->model[0] != '\0') {
        return (size_t)snprintf(out, out_cap, "%s", message->model);
    }

    if (message->provider_id != NULL && message->provider_id[0] != '\0') {
        return (size_t)snprintf(out, out_cap, "%s", message->provider_id);
    }

    return 0;
}

void agnc_conversation_mark_unsynced_bg(agnc_conversation_t *conversation, int job_id)
{
    size_t index;
    size_t start;

    if (conversation == NULL || conversation->unsynced_count == 0) {
        return;
    }

    start = conversation->count - conversation->unsynced_count;
    for (index = start; index < conversation->count; index++) {
        conversation->items[index].is_bg = 1;
        conversation->items[index].job_id = job_id;
    }
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

agnc_status_t agnc_conversation_trim_memory(agnc_conversation_t *conversation)
{
    size_t remove_index;
    size_t persisted_in_memory;

    /* Hanya buang pesan yang sudah ada di SQLite; unsynced suffix tetap di RAM. */
    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    while (conversation->count > AGNC_CONVERSATION_MEMORY_LIMIT) {
        persisted_in_memory = conversation->count - conversation->unsynced_count;
        if (persisted_in_memory == 0) {
            break;
        }

        remove_index = 0;
        if (conversation->count > 0 && conversation->items[0].role != NULL &&
            strcmp(conversation->items[0].role, "system") == 0) {
            if (persisted_in_memory <= 1) {
                break;
            }
            remove_index = 1;
        }

        if (remove_index >= conversation->count - conversation->unsynced_count) {
            break;
        }

        agnc_conversation_remove_at(conversation, remove_index);
    }

    return AGNC_STATUS_OK;
}

agnc_status_t agnc_conversation_compact(agnc_conversation_t *conversation, size_t keep_tail_messages)
{
    size_t remove_index;

    if (conversation == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    while (conversation->count > 0) {
        size_t non_system_count = 0;
        size_t index;

        for (index = 0; index < conversation->count; index++) {
            if (conversation->items[index].role != NULL && strcmp(conversation->items[index].role, "system") != 0) {
                non_system_count++;
            }
        }

        if (non_system_count <= keep_tail_messages) {
            break;
        }

        remove_index = 0;
        if (conversation->count > 0 && conversation->items[0].role != NULL &&
            strcmp(conversation->items[0].role, "system") == 0) {
            if (conversation->count <= 1) {
                break;
            }
            remove_index = 1;
        }

        agnc_conversation_remove_at(conversation, remove_index);
    }

    return AGNC_STATUS_OK;
}

size_t agnc_conversation_non_system_count(const agnc_conversation_t *conversation)
{
    size_t count = 0;
    size_t index;

    if (conversation == NULL) {
        return 0;
    }

    for (index = 0; index < conversation->count; index++) {
        const char *role = conversation->items[index].role;

        if (role != NULL && strcmp(role, "system") != 0) {
            count++;
        }
    }

    return count;
}

size_t agnc_conversation_llm_start_index(const agnc_conversation_t *conversation)
{
    size_t tail_budget = AGNC_CONVERSATION_LLM_WINDOW;
    int has_system = 0;

    if (conversation == NULL || conversation->count == 0) {
        return 0;
    }

    if (conversation->items[0].role != NULL && strcmp(conversation->items[0].role, "system") == 0) {
        has_system = 1;
        if (conversation->count <= 1 + tail_budget) {
            return 0;
        }
        return conversation->count - tail_budget;
    }

    if (conversation->count <= tail_budget) {
        return 0;
    }

    return conversation->count - tail_budget;
}

int agnc_conversation_llm_needs_summary(const agnc_conversation_t *conversation)
{
    if (conversation == NULL) {
        return 0;
    }

    return conversation->memory_skipped > 0 ||
        (conversation->db_total > conversation->count && conversation->count > 0);
}
