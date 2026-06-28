/*
 * cost.h
 *
 * Estimasi biaya USD dari token usage (tarif heuristik per model).
 */

#ifndef AGNC_COST_H
#define AGNC_COST_H

#include "agnc/status.h"

/* Estimasi biaya turn (USD) dari model + token; 0 jika tarif tidak dikenal. */
double agnc_cost_estimate_turn_usd(
    const char *model,
    const char *provider_id,
    long prompt_tokens,
    long completion_tokens);

/* Format biaya untuk tampilan (mis. "$0.0123"). Pemanggil free() hasilnya. */
char *agnc_cost_format_usd(double amount_usd);

#endif /* AGNC_COST_H */
