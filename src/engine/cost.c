/*
 * cost.c
 *
 * Estimasi biaya USD dari token (tarif heuristik; bukan billing resmi).
 */

#include "agnc/cost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *needle;
    double input_per_m;
    double output_per_m;
} agnc_cost_rate_t;

static const agnc_cost_rate_t g_cost_rates[] = {
    {"claude-opus", 15.0, 75.0},
    {"claude-sonnet", 3.0, 15.0},
    {"claude-haiku", 0.25, 1.25},
    {"gpt-4", 10.0, 30.0},
    {"gpt-5", 5.0, 15.0},
    {"gemini-2.5-pro", 1.25, 10.0},
    {"gemini-2.5-flash", 0.15, 0.6},
    {"qwen", 0.5, 1.5},
    {"llama", 0.2, 0.2},
    {NULL, 1.0, 3.0},
};

static const agnc_cost_rate_t *agnc_cost_find_rate(const char *model)
{
    size_t index;
    const agnc_cost_rate_t *fallback = &g_cost_rates[sizeof(g_cost_rates) / sizeof(g_cost_rates[0]) - 1];

    if (model == NULL || model[0] == '\0') {
        return fallback;
    }

    for (index = 0; g_cost_rates[index].needle != NULL; index++) {
        if (strstr(model, g_cost_rates[index].needle) != NULL) {
            return &g_cost_rates[index];
        }
    }

    return fallback;
}

double agnc_cost_estimate_turn_usd(
    const char *model,
    const char *provider_id,
    long prompt_tokens,
    long completion_tokens)
{
    const agnc_cost_rate_t *rate;
    double cost;

    (void)provider_id;

    if (prompt_tokens < 0) {
        prompt_tokens = 0;
    }
    if (completion_tokens < 0) {
        completion_tokens = 0;
    }
    if (prompt_tokens == 0 && completion_tokens == 0) {
        return 0.0;
    }

    rate = agnc_cost_find_rate(model);
    cost = ((double)prompt_tokens / 1000000.0) * rate->input_per_m +
           ((double)completion_tokens / 1000000.0) * rate->output_per_m;
    return cost;
}

char *agnc_cost_format_usd(double amount_usd)
{
    char *text = (char *)malloc(32);

    if (text == NULL) {
        return NULL;
    }

    if (amount_usd < 0.0001) {
        snprintf(text, 32, "$%.6f", amount_usd);
    } else if (amount_usd < 0.01) {
        snprintf(text, 32, "$%.4f", amount_usd);
    } else {
        snprintf(text, 32, "$%.4f", amount_usd);
    }

    return text;
}
