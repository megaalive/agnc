/*
 * conversation.h
 *
 * Daftar pesan multi-turn untuk agent loop (system, user, assistant, tool).
 * Buffer dinamis di RAM; riwayat penuh di SQLite sesi.
 */

#ifndef AGNC_CONVERSATION_H
#define AGNC_CONVERSATION_H

#include "agnc/status.h"

#include <stddef.h>
#include <stdint.h>

/* Pesan dimuat ke RAM (lazy load dari SQLite). */
#define AGNC_CONVERSATION_MEMORY_LIMIT 48

/* Pesan non-system maksimum dikirim ke LLM per request (windowed context). */
#define AGNC_CONVERSATION_LLM_WINDOW 32

#define AGNC_CONVERSATION_INITIAL_CAPACITY 16

/* Default /compact: keep N pesan tail (+ system). */
#define AGNC_CONVERSATION_COMPACT_KEEP 24

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    char *tool_name;
    char *tool_arguments;
    int64_t parent_id; /* 0 = NULL di SQLite */
    int is_bg;
    int job_id; /* 0 = NULL di SQLite */
} agnc_conversation_message_t;

typedef struct {
    agnc_conversation_message_t *items;
    size_t count;
    size_t capacity;
    size_t db_total;         /* total baris messages di SQLite */
    size_t memory_skipped;   /* pesan lama di DB, tidak ada di RAM */
    size_t unsynced_count;   /* suffix items[] belum disync ke DB */
    char *history_summary;   /* ringkasan untuk windowed context LLM */
} agnc_conversation_t;

void agnc_conversation_init(agnc_conversation_t *conversation);
void agnc_conversation_clear(agnc_conversation_t *conversation);

const agnc_conversation_message_t *agnc_conversation_at(const agnc_conversation_t *conversation, size_t index);

agnc_status_t agnc_conversation_push(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments);

/* Muat pesan dari SQLite ke RAM tanpa menandai unsynced. */
agnc_status_t agnc_conversation_push_hydrated(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments);

agnc_status_t agnc_conversation_ensure_system(agnc_conversation_t *conversation, const char *system_prompt);

/* Potong RAM ke memory limit; pesan yang di-drop tetap di SQLite. */
agnc_status_t agnc_conversation_trim_memory(agnc_conversation_t *conversation);

/*
 * Ringkas: pertahankan system + N pesan tail di RAM.
 * Pemanggil wajib memanggil agnc_session_compact_storage untuk selaraskan DB.
 */
agnc_status_t agnc_conversation_compact(agnc_conversation_t *conversation, size_t keep_tail_messages);

/* Indeks awal items[] untuk window LLM (system + tail). */
size_t agnc_conversation_llm_start_index(const agnc_conversation_t *conversation);

/* 1 jika perlu sisipkan pesan ringkasan riwayat sebelum tail. */
int agnc_conversation_llm_needs_summary(const agnc_conversation_t *conversation);

/* Tandai suffix unsynced dengan metadata bg sebelum agnc_session_sync. */
void agnc_conversation_mark_unsynced_bg(agnc_conversation_t *conversation, int job_id);

#endif /* AGNC_CONVERSATION_H */
