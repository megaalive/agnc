/*
 * query.h
 *
 * Agent loop: kirim prompt ke provider, stream jawaban, eksekusi tool, ulangi.
 */

#ifndef AGNC_QUERY_H
#define AGNC_QUERY_H

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/mcp/session.h"
#include "agnc/status.h"

typedef void (*agnc_query_stream_delta_fn)(const char *text, size_t length, void *user_data);

typedef struct {
    volatile int *cancel_flag; /* Ctrl+C: set ke 1 untuk batalkan request HTTP aktif. */
    int stream_live_print;     /* Cetak delta SSE ke stdout (REPL tidak memakai). */
    agnc_query_stream_delta_fn stream_delta_fn; /* Headless/gRPC: delta ke callback. */
    void *stream_delta_ctx;
    char **error_message_out;  /* Diisi detail error HTTP/provider jika gagal. */
    int chat_assistant_timestamp; /* REPL: cetak timestamp asisten saat jawaban siap (bukan saat mulai). */
    int auto_approve;          /* Setujui shell/write/mcp/web_fetch tanpa prompt (headless --yes). */
    agnc_mcp_session_t *mcp_session; /* REPL: koneksi MCP persist; NULL = load per run. */
    long *usage_prompt_tokens;     /* Opsional: akumulasi prompt tokens seluruh turn (-1 = belum ada). */
    long *usage_completion_tokens;
    long *usage_total_tokens;
    const char *session_name;      /* Opsional: nama sesi untuk hook payload. */
    const char *session_sqlite_path; /* Opsional: path ~/.agnc/sessions/<nama>.sqlite (OpenCode meta). */
    int agent_depth;               /* Kedalaman sub_agent (0 = turn utama). */
    int suppress_chat_output;      /* Job background: jangan cetak ke stdout (hindari timpa prompt). */
} agnc_query_options_t;

/*
 * Jalankan satu atau lebih turn agent pada conversation.
 * Jika user_prompt non-NULL, pesan user ditambahkan sebelum turn.
 */
agnc_status_t agnc_query_run(
    agnc_config_t *config,
    agnc_conversation_t *conversation,
    const char *user_prompt,
    const agnc_query_options_t *options);

/* Jalankan satu sesi headless --print dengan prompt pengguna. */
agnc_status_t agnc_query_print(
    const agnc_config_t *config,
    const char *prompt,
    const agnc_query_options_t *options);

/* System prompt default saat tools aktif/nonaktif. */
const char *agnc_query_default_system_prompt(int enable_tools, int search_only);

#endif /* AGNC_QUERY_H */
