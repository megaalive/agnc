/*
 * conversation.h
 *
 * Daftar pesan multi-turn untuk agent loop (system, user, assistant, tool).
 */

#ifndef AGNC_CONVERSATION_H
#define AGNC_CONVERSATION_H

#include "agnc/status.h"

#include <stddef.h>

#define AGNC_MAX_MESSAGES 64

/* Auto-ringkas saat mendekati batas (mis. output tool `dir` membesar). */
#define AGNC_CONVERSATION_COMPACT_THRESHOLD 52
#define AGNC_CONVERSATION_COMPACT_KEEP 24

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    char *tool_name;
    char *tool_arguments;
} agnc_conversation_message_t;

typedef struct {
    agnc_conversation_message_t items[AGNC_MAX_MESSAGES];
    size_t count;
} agnc_conversation_t;

void agnc_conversation_init(agnc_conversation_t *conversation);
void agnc_conversation_clear(agnc_conversation_t *conversation);

agnc_status_t agnc_conversation_push(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments);

/* Pastikan system prompt ada; perbarui isi jika sudah ada (workspace root, dll.). */
agnc_status_t agnc_conversation_ensure_system(agnc_conversation_t *conversation, const char *system_prompt);

/* Ringkas otomatis jika count >= threshold. */
agnc_status_t agnc_conversation_compact_if_needed(
    agnc_conversation_t *conversation,
    size_t threshold,
    size_t keep_tail_messages);

/*
 * Ringkas riwayat: pertahankan system (jika ada) + N pasangan pesan terakhir.
 * Tool message dihitung sebagai satu pesan dalam pasangan.
 */
agnc_status_t agnc_conversation_compact(agnc_conversation_t *conversation, size_t keep_tail_messages);

#endif /* AGNC_CONVERSATION_H */
