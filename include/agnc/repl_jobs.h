/*
 * repl_jobs.h
 *
 * Worker thread tunggal + antrean FIFO untuk prompt background REPL.
 */

#ifndef AGNC_REPL_JOBS_H
#define AGNC_REPL_JOBS_H

#include "agnc/config.h"
#include "agnc/status.h"

#define AGNC_REPL_JOB_QUEUE_MAX 16

typedef enum {
    AGNC_REPL_JOB_IDLE = 0,
    AGNC_REPL_JOB_RUNNING,
    AGNC_REPL_JOB_DONE,
    AGNC_REPL_JOB_CANCELLED,
    AGNC_REPL_JOB_FAILED
} agnc_repl_job_state_t;

void agnc_repl_jobs_init(void);
void agnc_repl_jobs_shutdown(void);

/* Poll completion; cetak notifikasi ke stdout. Return 1 jika ada output notifikasi. */
int agnc_repl_jobs_poll(void);

int agnc_repl_jobs_has_running(void);

int agnc_repl_jobs_queue_length(void);

/* True saat job aktif, antrean non-kosong, atau notifikasi selesai belum dipoll. */
int agnc_repl_jobs_wants_idle_poll(void);

void agnc_repl_jobs_handle_pending_permission(void);

int agnc_repl_jobs_has_pending_permission(void);

volatile int *agnc_repl_jobs_running_cancel_flag(void);

/*
 * Submit prompt ke sesi bg-<id>.
 * Return 0 OK, 1 error (antrean penuh / OOM).
 * *out_queued = 1 jika masuk antrean (worker masih sibuk).
 */
int agnc_repl_job_submit(
    const char *prompt,
    const agnc_config_t *parent_config,
    int parent_auto_approve,
    unsigned *out_id,
    int *out_queued);

void agnc_repl_jobs_print_status(void);

int agnc_repl_job_cancel_running(void);

/* Hapus semua job di antrean (bukan yang sedang running). Return jumlah yang dihapus. */
int agnc_repl_jobs_clear_queue(void);

int agnc_repl_jobs_get_running(unsigned *id_out, char *session_name, size_t session_name_cap);

#endif /* AGNC_REPL_JOBS_H */
