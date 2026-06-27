/*
 * skills.h
 *
 * Muat file markdown skills ke konteks system prompt (Fase 6.12).
 */

#ifndef AGNC_SKILLS_H
#define AGNC_SKILLS_H

#include "agnc/config.h"
#include "agnc/status.h"

#include <stddef.h>

typedef struct {
    char *name;
    char *path;
    size_t size_bytes;
} agnc_skill_entry_t;

/* Kosongkan cache skills (awal sesi REPL atau /skills reload). */
void agnc_skills_invalidate(void);

/*
 * Bangun blok teks skills untuk system prompt.
 * Return OK dengan *context_out NULL jika disabled atau tidak ada file.
 * Pemanggil free(*context_out).
 */
agnc_status_t agnc_skills_build_context(const agnc_config_t *config, char **context_out);

agnc_status_t agnc_skills_list(const agnc_config_t *config, agnc_skill_entry_t **entries_out, size_t *count_out);

void agnc_skills_list_free(agnc_skill_entry_t *entries, size_t count);

#endif
