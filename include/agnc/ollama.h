/*
 * ollama.h
 *
 * Probe ketersediaan Ollama lokal (OpenAI-compatible /v1).
 */

#ifndef AGNC_OLLAMA_H
#define AGNC_OLLAMA_H

#include "agnc/status.h"

#include <stddef.h>

#define AGNC_OLLAMA_DEFAULT_BASE_URL "http://127.0.0.1:11434/v1"

/*
 * Cek GET /v1/models. Return OK jika server merespons JSON valid.
 * model_count_out boleh NULL; detail buffer opsional (mis. "3 model(s)").
 */
agnc_status_t agnc_ollama_probe(
    const char *base_url_v1,
    size_t *model_count_out,
    char *detail,
    size_t detail_size);

#endif /* AGNC_OLLAMA_H */
