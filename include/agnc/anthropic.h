/*
 * anthropic.h
 *
 * Client native Anthropic Messages API (/v1/messages).
 */

#ifndef AGNC_ANTHROPIC_H
#define AGNC_ANTHROPIC_H

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/mcp/tools.h"
#include "agnc/net/sse.h"
#include "agnc/status.h"

agnc_status_t agnc_anthropic_probe(const char *base_url, const char *api_key, char *detail, size_t detail_size);

agnc_status_t agnc_anthropic_run_turn(
    const agnc_config_t *config,
    const agnc_conversation_t *conversation,
    const agnc_mcp_tool_catalog_t *mcp_catalog,
    agnc_sse_parser_t *parser,
    char **error_message,
    volatile int *cancel_flag);

#endif /* AGNC_ANTHROPIC_H */
