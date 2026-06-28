/*
 * grpc_bridge_link_stubs.c
 *
 * Stub simbol berat agnc_query_run untuk test_grpc_bridge (hanya jalur invalid_argument).
 */

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/permissions.h"
#include "agnc/query.h"
#include "agnc/session.h"
#include "agnc/status.h"

agnc_status_t agnc_config_load(const char *path, agnc_config_t *config)
{
    (void)path;
    (void)config;
    return AGNC_STATUS_IO_ERROR;
}

void agnc_config_init(agnc_config_t *config)
{
    (void)config;
}

void agnc_config_free(agnc_config_t *config)
{
    (void)config;
}

void agnc_conversation_init(agnc_conversation_t *conversation)
{
    (void)conversation;
}

void agnc_conversation_clear(agnc_conversation_t *conversation)
{
    (void)conversation;
}

const agnc_conversation_message_t *agnc_conversation_at(const agnc_conversation_t *conversation, size_t index)
{
    (void)conversation;
    (void)index;
    return NULL;
}

agnc_status_t agnc_query_run(
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    const char *user_prompt,
    const agnc_query_options_t *options)
{
    (void)config;
    (void)conversation;
    (void)user_prompt;
    (void)options;
    return AGNC_STATUS_OK;
}

agnc_status_t agnc_session_path_for_name(const char *session_name, char **path_out)
{
    (void)session_name;
    (void)path_out;
    return AGNC_STATUS_INVALID_ARGUMENT;
}

void agnc_permission_set_background_ask(agnc_permission_background_ask_fn fn, void *ctx)
{
    (void)fn;
    (void)ctx;
}

void agnc_permission_clear_background_ask(void)
{
}
