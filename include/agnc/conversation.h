/*
 * conversation.h
 *
 * Daftar pesan multi-turn untuk agent loop (system, user, assistant, tool).
 * Buffer dinamis di RAM; riwayat penuh di SQLite sesi.
 */

#ifndef AGNC_CONVERSATION_H
#define AGNC_CONVERSATION_H

#include "agnc/config.h"
#include "agnc/status.h"

#include <stdint.h>

/* Pesan dimuat ke RAM (lazy load dari SQLite). */
#define AGNC_CONVERSATION_MEMORY_LIMIT 48

/* Pesan non-system maksimum dikirim ke LLM per request (windowed context). */
#define AGNC_CONVERSATION_LLM_WINDOW 32

#define AGNC_CONVERSATION_INITIAL_CAPACITY 16

/* Default /compact: keep N pesan tail (+ system). */
#define AGNC_CONVERSATION_COMPACT_KEEP 24

/* Auto-compact REPL saat pesan non-system di RAM atau token sesi melewati batas. */
#define AGNC_SESSION_AUTO_COMPACT_THRESHOLD_MESSAGES 32
#define AGNC_SESSION_AUTO_COMPACT_THRESHOLD_TOKENS 100000L

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    char *tool_name;
    char *tool_arguments;
    int64_t parent_id; /* 0 = NULL di SQLite */
    int is_bg;
    int job_id; /* 0 = NULL di SQLite */
    char *provider_id;
    char *gateway_id;
    char *model;
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

agnc_status_t agnc_conversation_push_hydrated_routing(
    agnc_conversation_t *conversation,
    const char *role,
    const char *content,
    const char *tool_call_id,
    const char *tool_name,
    const char *tool_arguments,
    const char *provider_id,
    const char *gateway_id,
    const char *model);

/* Salin provider/gateway/model ke pesan terakhir (turn aktif). */
void agnc_conversation_apply_config_routing_to_last(
    agnc_conversation_t *conversation,
    const agnc_config_t *config);

/* Label singkat untuk UI, mis. "ollama/qwen2.5". Return 0 jika kosong. */
size_t agnc_conversation_format_routing_label(
    const agnc_conversation_message_t *message,
    char *out,
    size_t out_cap);

agnc_status_t agnc_conversation_ensure_system(agnc_conversation_t *conversation, const char *system_prompt);

/* Potong RAM ke memory limit; pesan yang di-drop tetap di SQLite. */
agnc_status_t agnc_conversation_trim_memory(agnc_conversation_t *conversation);

/*
 * Ringkas RAM: pertahankan system + N pesan tail.
 * Pesan yang dibuang tetap di SQLite; memory_skipped naik untuk window LLM.
 */
agnc_status_t agnc_conversation_compact(agnc_conversation_t *conversation, size_t keep_tail_messages);

/* Jumlah pesan non-system yang ada di RAM. */
size_t agnc_conversation_non_system_count(const agnc_conversation_t *conversation);

/* Indeks awal items[] untuk window LLM (system + tail). */
size_t agnc_conversation_llm_start_index(const agnc_conversation_t *conversation);

/* 1 jika perlu sisipkan pesan ringkasan riwayat sebelum tail. */
int agnc_conversation_llm_needs_summary(const agnc_conversation_t *conversation);

/* Tandai suffix unsynced dengan metadata bg sebelum agnc_session_sync. */
void agnc_conversation_mark_unsynced_bg(agnc_conversation_t *conversation, int job_id);

#endif /* AGNC_CONVERSATION_H */
