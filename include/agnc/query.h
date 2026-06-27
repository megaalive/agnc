/*
 * query.h
 *
 * Agent loop: kirim prompt ke provider, stream jawaban, eksekusi tool, ulangi.
 */

#ifndef AGNC_QUERY_H
#define AGNC_QUERY_H

#include "agnc/config.h"
#include "agnc/conversation.h"
#include "agnc/status.h"

typedef struct {
    volatile int *cancel_flag; /* Ctrl+C: set ke 1 untuk batalkan request HTTP aktif. */
    int stream_live_print;     /* Reserved: cetak delta SSE langsung (tidak dipakai REPL). */
    int chat_assistant_timestamp; /* REPL: cetak timestamp asisten saat jawaban siap (bukan saat mulai). */
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
agnc_status_t agnc_query_print(const agnc_config_t *config, const char *prompt);

/* System prompt default saat tools aktif/nonaktif. */
const char *agnc_query_default_system_prompt(int enable_tools, int search_only);

#endif /* AGNC_QUERY_H */
