/*
 * repl_jobs.c
 *
 * FIFO background jobs (max 16) di REPL: worker tunggal, auto-start berikutnya.
 * Permission prompt di-delegate ke thread REPL via agnc_permission_set_background_ask.
 */

#include "agnc/repl_jobs.h"

#include "agnc/config.h"
#include "agnc/console.h"
#include "agnc/conversation.h"
#include "agnc/mcp/session.h"
#include "agnc/permissions.h"
#include "agnc/query.h"
#include "agnc/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#endif

typedef enum {
    AGNC_REPL_JOB_PERM_NONE = 0,
    AGNC_REPL_JOB_PERM_SHELL,
    AGNC_REPL_JOB_PERM_WRITE,
    AGNC_REPL_JOB_PERM_MCP,
    AGNC_REPL_JOB_PERM_WEB_FETCH
} agnc_repl_job_perm_kind_t;

typedef struct {
    agnc_repl_job_state_t state;
    volatile int cancel_flag;
    unsigned id;
    char *prompt;
    char *session_name;
    char *session_path;
    char *summary;
    agnc_config_t config;
    agnc_conversation_t conversation;
    agnc_mcp_session_t mcp_session;
    int auto_approve;
    agnc_repl_job_perm_kind_t pending_perm;
    char *pending_perm_detail;
    int perm_result; /* -1 menunggu, 0 tolak, 1 izinkan */
#ifdef _WIN32
    HANDLE thread;
    CONDITION_VARIABLE perm_cv;
#else
    pthread_t thread;
    pthread_cond_t perm_cv;
#endif
} agnc_repl_job_t;

typedef struct {
    unsigned id;
    char *prompt;
    char *session_name;
    char *session_path;
    agnc_config_t config;
    int auto_approve;
} agnc_repl_job_spec_t;

static agnc_repl_job_t g_job;
static agnc_repl_job_spec_t g_queue[AGNC_REPL_JOB_QUEUE_MAX];
static size_t g_queue_count = 0;
static unsigned g_next_job_id = 1;
static int g_perm_sync_ready = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_job_lock;
static int g_job_lock_ready = 0;

static void agnc_repl_jobs_perm_sync_init(void)
{
    if (!g_perm_sync_ready) {
        InitializeConditionVariable(&g_job.perm_cv);
        g_perm_sync_ready = 1;
    }
}

static void agnc_repl_jobs_perm_wake(void)
{
    WakeAllConditionVariable(&g_job.perm_cv);
}

static void agnc_repl_jobs_perm_wait_locked(void)
{
    while (g_job.perm_result == -1 && !g_job.cancel_flag) {
        SleepConditionVariableCS(&g_job.perm_cv, &g_job_lock, 100);
    }
}

static void agnc_repl_jobs_lock_init(void)
{
    if (!g_job_lock_ready) {
        InitializeCriticalSection(&g_job_lock);
        g_job_lock_ready = 1;
    }
}

static void agnc_repl_jobs_lock(void)
{
    agnc_repl_jobs_lock_init();
    EnterCriticalSection(&g_job_lock);
}

static void agnc_repl_jobs_unlock(void)
{
    LeaveCriticalSection(&g_job_lock);
}
#else
static pthread_mutex_t g_job_lock = PTHREAD_MUTEX_INITIALIZER;

static void agnc_repl_jobs_perm_sync_init(void)
{
    if (!g_perm_sync_ready) {
        pthread_cond_init(&g_job.perm_cv, NULL);
        g_perm_sync_ready = 1;
    }
}

static void agnc_repl_jobs_perm_wake(void)
{
    pthread_cond_broadcast(&g_job.perm_cv);
}

static void agnc_repl_jobs_perm_wait_locked(void)
{
    while (g_job.perm_result == -1 && !g_job.cancel_flag) {
        struct timespec wait_until;
        struct timeval now;

        gettimeofday(&now, NULL);
        wait_until.tv_sec = now.tv_sec;
        wait_until.tv_nsec = (long)now.tv_usec * 1000L + 100000000L;
        if (wait_until.tv_nsec >= 1000000000L) {
            wait_until.tv_sec++;
            wait_until.tv_nsec -= 1000000000L;
        }
        (void)pthread_cond_timedwait(&g_job.perm_cv, &g_job_lock, &wait_until);
    }
}

static void agnc_repl_jobs_lock(void)
{
    (void)pthread_mutex_lock(&g_job_lock);
}

static void agnc_repl_jobs_unlock(void)
{
    (void)pthread_mutex_unlock(&g_job_lock);
}
#endif

static char *agnc_strdup_local(const char *value)
{
#ifdef _MSC_VER
    return _strdup(value);
#else
    return strdup(value);
#endif
}

static void agnc_repl_job_reset_locked(void)
{
    g_job.state = AGNC_REPL_JOB_IDLE;
    g_job.cancel_flag = 0;
    g_job.pending_perm = AGNC_REPL_JOB_PERM_NONE;
    g_job.perm_result = -1;
    free(g_job.prompt);
    free(g_job.session_name);
    free(g_job.session_path);
    free(g_job.summary);
    free(g_job.pending_perm_detail);
    g_job.prompt = NULL;
    g_job.session_name = NULL;
    g_job.session_path = NULL;
    g_job.summary = NULL;
    g_job.pending_perm_detail = NULL;
    agnc_config_free(&g_job.config);
    agnc_conversation_clear(&g_job.conversation);
    agnc_mcp_session_free(&g_job.mcp_session);
    agnc_config_init(&g_job.config);
    agnc_conversation_init(&g_job.conversation);
    agnc_mcp_session_init(&g_job.mcp_session);
}

static void agnc_repl_job_spec_free(agnc_repl_job_spec_t *spec)
{
    if (spec == NULL) {
        return;
    }

    free(spec->prompt);
    free(spec->session_name);
    free(spec->session_path);
    spec->prompt = NULL;
    spec->session_name = NULL;
    spec->session_path = NULL;
    agnc_config_free(&spec->config);
    agnc_config_init(&spec->config);
}

static int agnc_repl_job_config_copy(agnc_config_t *dst, const agnc_config_t *parent_config)
{
    agnc_config_init(dst);
    dst->base_url = agnc_strdup_local(parent_config->base_url);
    dst->model = agnc_strdup_local(parent_config->model);
    dst->api_key = agnc_strdup_local(parent_config->api_key);
    dst->provider_id = agnc_strdup_local(parent_config->provider_id);
    dst->gateway_id = agnc_strdup_local(parent_config->gateway_id);
    if ((parent_config->base_url != NULL && dst->base_url == NULL) ||
        (parent_config->model != NULL && dst->model == NULL) ||
        (parent_config->api_key != NULL && dst->api_key == NULL) ||
        (parent_config->provider_id != NULL && dst->provider_id == NULL) ||
        (parent_config->gateway_id != NULL && dst->gateway_id == NULL)) {
        return 1;
    }

    dst->max_tool_iterations = parent_config->max_tool_iterations;
    dst->stream = 0;
    dst->verbose = parent_config->verbose;
    dst->enable_tools = parent_config->enable_tools;
    dst->tool_read_file = parent_config->tool_read_file;
    dst->tool_shell = parent_config->tool_shell;
    dst->tool_write_file = parent_config->tool_write_file;
    dst->tool_edit_file = parent_config->tool_edit_file;
    dst->tool_grep = parent_config->tool_grep;
    dst->tool_glob = parent_config->tool_glob;
    dst->tool_web_fetch = parent_config->tool_web_fetch;
    dst->tool_todo_write = parent_config->tool_todo_write;
    dst->tool_find_symbol = parent_config->tool_find_symbol;
    dst->tool_sub_agent = 0;
    dst->ask_shell_permission = parent_config->ask_shell_permission;
    dst->ask_write_permission = parent_config->ask_write_permission;
    dst->ask_mcp_permission = parent_config->ask_mcp_permission;
    dst->ask_web_fetch_permission = parent_config->ask_web_fetch_permission;
    dst->deny_shell_permission = parent_config->deny_shell_permission;
    dst->deny_write_permission = parent_config->deny_write_permission;
    return 0;
}

static int agnc_repl_job_spec_init(
    agnc_repl_job_spec_t *spec,
    const char *prompt,
    const agnc_config_t *parent_config,
    int parent_auto_approve,
    unsigned id)
{
    char session_name[64];
    agnc_status_t status;

    memset(spec, 0, sizeof(*spec));
    agnc_config_init(&spec->config);
    spec->id = id;
    spec->auto_approve = parent_auto_approve;
    spec->prompt = agnc_strdup_local(prompt);
    if (spec->prompt == NULL) {
        goto fail;
    }

    snprintf(session_name, sizeof(session_name), "bg-%u", id);
    spec->session_name = agnc_strdup_local(session_name);
    status = agnc_session_path_for_name(session_name, &spec->session_path);
    if (status != AGNC_STATUS_OK || spec->session_name == NULL) {
        goto fail;
    }

    if (agnc_repl_job_config_copy(&spec->config, parent_config) != 0) {
        goto fail;
    }

    return 0;

fail:
    agnc_repl_job_spec_free(spec);
    return 1;
}

static void agnc_repl_job_print_started(unsigned id, const char *session_name, int parent_auto_approve)
{
    printf("background job %u dimulai (sesi %s)\n", id, session_name);
    if (!parent_auto_approve && !agnc_permission_session_has_shell()) {
        printf("  (tool shell/write butuh izin — prompt muncul di REPL saat job membutuhkannya)\n");
    }
}

#ifdef _WIN32
static DWORD WINAPI agnc_repl_job_thread_main(LPVOID unused);
#else
static void *agnc_repl_job_thread_main(void *unused);
#endif

static int agnc_repl_job_start_locked(agnc_repl_job_spec_t *spec)
{
    if (spec == NULL || g_job.state != AGNC_REPL_JOB_IDLE) {
        return 0;
    }

    g_job.id = spec->id;
    g_job.prompt = spec->prompt;
    g_job.session_name = spec->session_name;
    g_job.session_path = spec->session_path;
    g_job.auto_approve = spec->auto_approve;
    spec->prompt = NULL;
    spec->session_name = NULL;
    spec->session_path = NULL;

    agnc_config_free(&g_job.config);
    g_job.config = spec->config;
    agnc_config_init(&spec->config);

    g_job.state = AGNC_REPL_JOB_RUNNING;
    g_job.cancel_flag = 0;

#ifdef _WIN32
    g_job.thread = CreateThread(NULL, 0, agnc_repl_job_thread_main, NULL, 0, NULL);
    if (g_job.thread == NULL) {
        agnc_repl_job_reset_locked();
        return 0;
    }
#else
    if (pthread_create(&g_job.thread, NULL, agnc_repl_job_thread_main, NULL) != 0) {
        agnc_repl_job_reset_locked();
        return 0;
    }
#endif

    return 1;
}

static int agnc_repl_job_try_start_next_locked(unsigned *started_id, char *session_name, size_t session_name_cap)
{
    agnc_repl_job_spec_t spec;

    if (g_job.state != AGNC_REPL_JOB_IDLE || g_queue_count == 0) {
        return 0;
    }

    spec = g_queue[0];
    if (g_queue_count > 1) {
        memmove(&g_queue[0], &g_queue[1], (g_queue_count - 1) * sizeof(spec));
    }
    g_queue_count--;

    if (!agnc_repl_job_start_locked(&spec)) {
        agnc_repl_job_spec_free(&spec);
        return 0;
    }

    if (started_id != NULL) {
        *started_id = g_job.id;
    }
    if (session_name != NULL && session_name_cap > 0) {
        if (g_job.session_name != NULL) {
            snprintf(session_name, session_name_cap, "%s", g_job.session_name);
        } else {
            session_name[0] = '\0';
        }
    }
    return 1;
}

void agnc_repl_jobs_init(void)
{
#ifdef _WIN32
    agnc_repl_jobs_lock_init();
#endif
    agnc_repl_jobs_perm_sync_init();
    agnc_repl_jobs_lock();
    memset(&g_job, 0, sizeof(g_job));
    agnc_config_init(&g_job.config);
    agnc_conversation_init(&g_job.conversation);
    agnc_mcp_session_init(&g_job.mcp_session);
    g_job.state = AGNC_REPL_JOB_IDLE;
    g_queue_count = 0;
    agnc_repl_jobs_unlock();
}

void agnc_repl_jobs_shutdown(void)
{
    size_t index;

    agnc_repl_job_cancel_running();
    agnc_repl_jobs_poll();
    agnc_repl_jobs_lock();
    for (index = 0; index < g_queue_count; index++) {
        agnc_repl_job_spec_free(&g_queue[index]);
    }
    g_queue_count = 0;
    agnc_repl_jobs_unlock();
#ifdef _WIN32
    if (g_job_lock_ready) {
        DeleteCriticalSection(&g_job_lock);
        g_job_lock_ready = 0;
    }
#endif
}

static const char *agnc_repl_job_last_assistant(const agnc_conversation_t *conversation)
{
    size_t index;

    if (conversation == NULL) {
        return NULL;
    }

    for (index = conversation->count; index > 0; index--) {
        const agnc_conversation_message_t *message = agnc_conversation_at(conversation, index - 1);

        if (message != NULL && message->role != NULL && strcmp(message->role, "assistant") == 0 &&
            message->content != NULL && message->content[0] != '\0' && message->tool_name == NULL) {
            return message->content;
        }
    }

    return NULL;
}

static agnc_repl_job_perm_kind_t agnc_repl_job_perm_kind_from_label(const char *kind)
{
    if (kind == NULL) {
        return AGNC_REPL_JOB_PERM_NONE;
    }
    if (strcmp(kind, "shell") == 0) {
        return AGNC_REPL_JOB_PERM_SHELL;
    }
    if (strcmp(kind, "write") == 0 || strcmp(kind, "edit") == 0) {
        return AGNC_REPL_JOB_PERM_WRITE;
    }
    if (strcmp(kind, "mcp") == 0) {
        return AGNC_REPL_JOB_PERM_MCP;
    }
    if (strcmp(kind, "web_fetch") == 0) {
        return AGNC_REPL_JOB_PERM_WEB_FETCH;
    }
    return AGNC_REPL_JOB_PERM_NONE;
}

static int agnc_repl_job_permission_delegate(const char *kind, const char *detail, void *ctx)
{
    agnc_repl_job_perm_kind_t perm_kind;
    int allowed = 0;

    (void)ctx;

    perm_kind = agnc_repl_job_perm_kind_from_label(kind);
    if (perm_kind == AGNC_REPL_JOB_PERM_NONE) {
        return 0;
    }

    agnc_repl_jobs_lock();
    if (g_job.state != AGNC_REPL_JOB_RUNNING || g_job.cancel_flag) {
        agnc_repl_jobs_unlock();
        return 0;
    }

    free(g_job.pending_perm_detail);
    g_job.pending_perm = perm_kind;
    g_job.pending_perm_detail = detail != NULL ? agnc_strdup_local(detail) : NULL;
    g_job.perm_result = -1;
    agnc_repl_jobs_perm_wake();
    agnc_repl_jobs_perm_wait_locked();
    allowed = g_job.perm_result > 0 ? 1 : 0;
    g_job.pending_perm = AGNC_REPL_JOB_PERM_NONE;
    free(g_job.pending_perm_detail);
    g_job.pending_perm_detail = NULL;
    g_job.perm_result = -1;
    agnc_repl_jobs_unlock();

    return allowed;
}

#ifdef _WIN32
static DWORD WINAPI agnc_repl_job_thread_main(LPVOID unused)
#else
static void *agnc_repl_job_thread_main(void *unused)
#endif
{
    agnc_query_options_t options;
    agnc_status_t status;
    long usage_prompt = 0;
    long usage_completion = 0;
    long usage_total = 0;

    (void)unused;

    memset(&options, 0, sizeof(options));
    options.cancel_flag = &g_job.cancel_flag;
    options.auto_approve = g_job.auto_approve;
    options.chat_assistant_timestamp = 0;
    options.suppress_chat_output = 1;
    options.mcp_session = &g_job.mcp_session;
    options.session_name = g_job.session_name;
    options.session_sqlite_path = g_job.session_path;
    options.usage_prompt_tokens = &usage_prompt;
    options.usage_completion_tokens = &usage_completion;
    options.usage_total_tokens = &usage_total;
    options.agent_depth = 0;

    agnc_permission_set_background_ask(agnc_repl_job_permission_delegate, NULL);
    status = agnc_query_run(&g_job.config, &g_job.conversation, g_job.prompt, &options);
    agnc_permission_clear_background_ask();
    if (g_job.session_path != NULL) {
        (void)agnc_session_sync(g_job.session_path, &g_job.conversation, &g_job.config);
    }

    agnc_repl_jobs_lock();
    if (status == AGNC_STATUS_CANCELLED) {
        g_job.state = AGNC_REPL_JOB_CANCELLED;
        g_job.summary = agnc_strdup_local("dibatalkan");
    } else if (status != AGNC_STATUS_OK) {
        g_job.state = AGNC_REPL_JOB_FAILED;
        g_job.summary = agnc_strdup_local(agnc_status_to_string(status));
    } else {
        const char *assistant = agnc_repl_job_last_assistant(&g_job.conversation);

        g_job.state = AGNC_REPL_JOB_DONE;
        g_job.summary = agnc_strdup_local(assistant != NULL ? assistant : "(kosong)");
    }
    agnc_repl_jobs_unlock();

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int agnc_repl_job_submit(
    const char *prompt,
    const agnc_config_t *parent_config,
    int parent_auto_approve,
    unsigned *out_id,
    int *out_queued)
{
    unsigned id;
    agnc_repl_job_spec_t spec;
    int started = 0;
    int queued = 0;
    size_t queue_pos = 0;
    char session_name[64];

    if (prompt == NULL || prompt[0] == '\0' || parent_config == NULL) {
        return 1;
    }

    agnc_repl_jobs_lock();
    id = g_next_job_id++;
    if (out_id != NULL) {
        *out_id = id;
    }
    if (out_queued != NULL) {
        *out_queued = 0;
    }

    if (agnc_repl_job_spec_init(&spec, prompt, parent_config, parent_auto_approve, id) != 0) {
        agnc_repl_jobs_unlock();
        return 1;
    }

    snprintf(session_name, sizeof(session_name), "bg-%u", id);

    if (g_job.state == AGNC_REPL_JOB_IDLE) {
        started = agnc_repl_job_start_locked(&spec);
        agnc_repl_job_spec_free(&spec);
    } else {
        if (g_queue_count >= AGNC_REPL_JOB_QUEUE_MAX) {
            agnc_repl_job_spec_free(&spec);
            agnc_repl_jobs_unlock();
            return 1;
        }
        g_queue[g_queue_count] = spec;
        g_queue_count++;
        queue_pos = g_queue_count;
        queued = 1;
        if (out_queued != NULL) {
            *out_queued = 1;
        }
    }
    agnc_repl_jobs_unlock();

    if (started) {
        agnc_repl_job_print_started(id, session_name, parent_auto_approve);
    } else if (queued) {
        printf(
            "background job %u diantri (posisi %zu, sesi %s)\n",
            id,
            queue_pos,
            session_name);
    } else if (!queued) {
        return 1;
    }

    return 0;
}

int agnc_repl_jobs_poll(void)
{
    int notify = 0;
    int started = 0;
    int started_auto_approve = 0;
    unsigned id = 0;
    unsigned started_id = 0;
    char *summary = NULL;
    char *session_name = NULL;
    char started_session[64];
    agnc_repl_job_state_t state = AGNC_REPL_JOB_IDLE;

    started_session[0] = '\0';

    agnc_repl_jobs_lock();
    if (g_job.state == AGNC_REPL_JOB_IDLE && g_queue_count > 0) {
        started = agnc_repl_job_try_start_next_locked(&started_id, started_session, sizeof(started_session));
    }

    if (g_job.state == AGNC_REPL_JOB_RUNNING && g_job.thread != NULL) {
#ifdef _WIN32
        if (WaitForSingleObject(g_job.thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_job.thread);
            g_job.thread = NULL;
        }
#endif
    }

    if (g_job.state == AGNC_REPL_JOB_DONE || g_job.state == AGNC_REPL_JOB_CANCELLED ||
        g_job.state == AGNC_REPL_JOB_FAILED) {
        if (g_job.thread != NULL) {
#ifdef _WIN32
            WaitForSingleObject(g_job.thread, INFINITE);
            CloseHandle(g_job.thread);
#else
            (void)pthread_join(g_job.thread, NULL);
#endif
            g_job.thread = NULL;
        }
        notify = 1;
        id = g_job.id;
        state = g_job.state;
        summary = g_job.summary != NULL ? agnc_strdup_local(g_job.summary) : NULL;
        session_name = g_job.session_name != NULL ? agnc_strdup_local(g_job.session_name) : NULL;
        agnc_repl_job_reset_locked();
        if (!started) {
            started = agnc_repl_job_try_start_next_locked(&started_id, started_session, sizeof(started_session));
        }
    }
    if (started) {
        started_auto_approve = g_job.auto_approve;
    }
    agnc_repl_jobs_unlock();

    if (started) {
        agnc_repl_job_print_started(
            started_id,
            started_session[0] != '\0' ? started_session : "?",
            started_auto_approve);
    }

    if (notify) {
        char header[160];

        agnc_console_clear_input_line();

        if (state == AGNC_REPL_JOB_DONE) {
            snprintf(
                header,
                sizeof(header),
                "background job %u selesai — sesi %s",
                id,
                session_name != NULL ? session_name : "?");
            agnc_console_print_chat_system(header);
            if (summary != NULL && summary[0] != '\0') {
                agnc_console_print_chat_assistant_begin();
                agnc_console_print_assistant_body(summary);
            }
        } else if (state == AGNC_REPL_JOB_CANCELLED) {
            snprintf(
                header,
                sizeof(header),
                "background job %u dibatalkan — sesi %s",
                id,
                session_name != NULL ? session_name : "?");
            agnc_console_print_chat_system(header);
        } else {
            if (summary != NULL && summary[0] != '\0') {
                snprintf(
                    header,
                    sizeof(header),
                    "background job %u gagal (%s) — sesi %s",
                    id,
                    summary,
                    session_name != NULL ? session_name : "?");
            } else {
                snprintf(
                    header,
                    sizeof(header),
                    "background job %u gagal — sesi %s",
                    id,
                    session_name != NULL ? session_name : "?");
            }
            agnc_console_print_chat_system(header);
        }

        free(summary);
        free(session_name);
    }

    return notify || started;
}

int agnc_repl_jobs_has_running(void)
{
    int running;

    agnc_repl_jobs_lock();
    running = g_job.state == AGNC_REPL_JOB_RUNNING ? 1 : 0;
    agnc_repl_jobs_unlock();
    return running;
}

int agnc_repl_jobs_queue_length(void)
{
    int length;

    agnc_repl_jobs_lock();
    length = (int)g_queue_count;
    agnc_repl_jobs_unlock();
    return length;
}

int agnc_repl_jobs_wants_idle_poll(void)
{
    int wants;

    agnc_repl_jobs_lock();
    wants = (g_job.state != AGNC_REPL_JOB_IDLE || g_queue_count > 0) ? 1 : 0;
    agnc_repl_jobs_unlock();
    return wants;
}

int agnc_repl_jobs_has_pending_permission(void)
{
    int pending;

    agnc_repl_jobs_lock();
    pending = g_job.state == AGNC_REPL_JOB_RUNNING && g_job.pending_perm != AGNC_REPL_JOB_PERM_NONE &&
              g_job.perm_result == -1;
    agnc_repl_jobs_unlock();
    return pending;
}

void agnc_repl_jobs_handle_pending_permission(void)
{
    agnc_repl_job_perm_kind_t kind;
    char *detail = NULL;
    int allowed = 0;

    agnc_repl_jobs_lock();
    if (g_job.state != AGNC_REPL_JOB_RUNNING || g_job.pending_perm == AGNC_REPL_JOB_PERM_NONE ||
        g_job.perm_result != -1) {
        agnc_repl_jobs_unlock();
        return;
    }
    kind = g_job.pending_perm;
    detail = g_job.pending_perm_detail != NULL ? agnc_strdup_local(g_job.pending_perm_detail) : NULL;
    agnc_repl_jobs_unlock();

    agnc_console_print_chat_system("background job membutuhkan izin tool");

    switch (kind) {
    case AGNC_REPL_JOB_PERM_SHELL:
        (void)agnc_permission_ask_shell(detail, &allowed, 1);
        break;
    case AGNC_REPL_JOB_PERM_WRITE:
        (void)agnc_permission_ask_file_write(detail, "write", &allowed, 1);
        break;
    case AGNC_REPL_JOB_PERM_MCP:
        (void)agnc_permission_ask_mcp(detail, &allowed, 1);
        break;
    case AGNC_REPL_JOB_PERM_WEB_FETCH:
        (void)agnc_permission_ask_web_fetch(detail, &allowed, 1);
        break;
    default:
        break;
    }

    agnc_repl_jobs_lock();
    if (g_job.state == AGNC_REPL_JOB_RUNNING && g_job.perm_result == -1) {
        g_job.perm_result = allowed ? 1 : 0;
        g_job.pending_perm = AGNC_REPL_JOB_PERM_NONE;
        free(g_job.pending_perm_detail);
        g_job.pending_perm_detail = NULL;
        agnc_repl_jobs_perm_wake();
    }
    agnc_repl_jobs_unlock();
    free(detail);
}

volatile int *agnc_repl_jobs_running_cancel_flag(void)
{
    if (!agnc_repl_jobs_has_running()) {
        return NULL;
    }
    return &g_job.cancel_flag;
}

void agnc_repl_jobs_print_status(void)
{
    size_t index;

    agnc_repl_jobs_lock();
    if (g_job.state == AGNC_REPL_JOB_IDLE && g_queue_count == 0) {
        printf("  (tidak ada job background)\n");
    } else {
        if (g_job.state != AGNC_REPL_JOB_IDLE) {
            printf("  job %u: ", g_job.id);
            switch (g_job.state) {
            case AGNC_REPL_JOB_RUNNING:
                if (g_job.pending_perm != AGNC_REPL_JOB_PERM_NONE) {
                    printf(
                        "waiting permission (%s) — sesi %s\n",
                        g_job.pending_perm_detail != NULL ? g_job.pending_perm_detail : "?",
                        g_job.session_name != NULL ? g_job.session_name : "?");
                } else {
                    printf("running — sesi %s\n", g_job.session_name != NULL ? g_job.session_name : "?");
                }
                break;
            case AGNC_REPL_JOB_DONE:
                printf("done\n");
                break;
            case AGNC_REPL_JOB_CANCELLED:
                printf("cancelled\n");
                break;
            case AGNC_REPL_JOB_FAILED:
                printf("failed\n");
                break;
            default:
                printf("?\n");
                break;
            }
        }
        for (index = 0; index < g_queue_count; index++) {
            printf(
                "  job %u: queued (posisi %zu) — sesi %s\n",
                g_queue[index].id,
                index + 1,
                g_queue[index].session_name != NULL ? g_queue[index].session_name : "?");
        }
    }
    agnc_repl_jobs_unlock();
}

int agnc_repl_job_cancel_running(void)
{
    agnc_repl_jobs_lock();
    if (g_job.state != AGNC_REPL_JOB_RUNNING) {
        agnc_repl_jobs_unlock();
        return 0;
    }
    g_job.cancel_flag = 1;
    agnc_repl_jobs_unlock();
    return 1;
}

int agnc_repl_jobs_clear_queue(void)
{
    size_t index;
    int cleared = 0;

    agnc_repl_jobs_lock();
    for (index = 0; index < g_queue_count; index++) {
        agnc_repl_job_spec_free(&g_queue[index]);
    }
    cleared = (int)g_queue_count;
    g_queue_count = 0;
    agnc_repl_jobs_unlock();
    return cleared;
}

int agnc_repl_jobs_get_running(unsigned *id_out, char *session_name, size_t session_name_cap)
{
    int running = 0;

    agnc_repl_jobs_lock();
    if (g_job.state == AGNC_REPL_JOB_RUNNING) {
        running = 1;
        if (id_out != NULL) {
            *id_out = g_job.id;
        }
        if (session_name != NULL && session_name_cap > 0) {
            if (g_job.session_name != NULL) {
                snprintf(session_name, session_name_cap, "%s", g_job.session_name);
            } else {
                session_name[0] = '\0';
            }
        }
    }
    agnc_repl_jobs_unlock();
    return running;
}
