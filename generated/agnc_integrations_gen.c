/* File ini dihasilkan oleh scripts/generate_integrations.py — jangan edit manual. */
#include "agnc_integrations_gen.h"

static const agnc_model_descriptor_t agnc_models_anthropic[] = {
    {"claude-sonnet-4-20250514", "claude-sonnet-4-20250514", 0, 1, 0},
    {"claude-3-5-haiku-20241022", "claude-3-5-haiku-20241022", 0, 1, 0},
};

static const char *const agnc_env_anthropic[] = {
    "ANTHROPIC_API_KEY",
    "AGNC_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_anthropic = {
    "anthropic",
    "Anthropic",
    "https://api.anthropic.com",
    "claude-sonnet-4-20250514",
    AGNC_TRANSPORT_ANTHROPIC_NATIVE,
    "x-api-key",
    "raw",
    "/v1/messages",
    "/models",
    0,
    1,
    1,
    agnc_env_anthropic,
    2,
    agnc_models_anthropic,
    2,
};

static const char *const agnc_env_custom_openai_compatible[] = {
    "AGNC_API_KEY",
    "OPENAI_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_custom_openai_compatible = {
    "custom-openai-compatible",
    "Custom OpenAI-compatible",
    "http://127.0.0.1:8080/v1",
    "gpt-4o-mini",
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    "Authorization",
    "bearer",
    "/chat/completions",
    "/models",
    1,
    1,
    0,
    agnc_env_custom_openai_compatible,
    2,
    NULL,
    0,
};

static const agnc_model_descriptor_t agnc_models_gemini[] = {
    {"gemini-2.0-flash", "gemini-2.0-flash", 1, 1, 0},
};

static const char *const agnc_env_gemini[] = {
    "GEMINI_API_KEY",
    "GOOGLE_API_KEY",
    "AGNC_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_gemini = {
    "gemini",
    "Google Gemini",
    "https://generativelanguage.googleapis.com/v1beta/openai",
    "gemini-2.0-flash",
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    "Authorization",
    "bearer",
    "/chat/completions",
    "/models",
    1,
    1,
    1,
    agnc_env_gemini,
    3,
    agnc_models_gemini,
    1,
};

static const char *const agnc_env_naraya[] = {
    "NARAYA_API_KEY",
    "AGNC_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_naraya = {
    "naraya",
    "Naraya",
    "https://api.naraya.ai/v1",
    "naraya/default",
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    "Authorization",
    "bearer",
    "/chat/completions",
    "/models",
    1,
    1,
    1,
    agnc_env_naraya,
    2,
    NULL,
    0,
};

static const char *const agnc_env_ollama[] = {
    "OLLAMA_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_ollama = {
    "ollama",
    "Ollama (lokal)",
    "http://127.0.0.1:11434/v1",
    "llama3.2",
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    "Authorization",
    "bearer",
    "/chat/completions",
    "/models",
    1,
    1,
    0,
    agnc_env_ollama,
    1,
    NULL,
    0,
};

static const char *const agnc_env_opencode_local[] = {
    "OPENCODE_SERVER_PASSWORD",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_opencode_local = {
    "opencode-local",
    "OpenCode (lokal)",
    "http://127.0.0.1:4096",
    "opencode/big-pickle",
    AGNC_TRANSPORT_OPENCODE_NATIVE,
    "Authorization",
    "basic",
    "/session/{sessionID}/message",
    "/config/providers",
    0,
    0,
    0,
    agnc_env_opencode_local,
    1,
    NULL,
    0,
};

static const agnc_model_descriptor_t agnc_models_openrouter[] = {
    {"owl-alpha", "openrouter/owl-alpha", 1, 1, 0},
};

static const char *const agnc_env_openrouter[] = {
    "OPENROUTER_API_KEY",
    "AGNC_API_KEY",
    NULL
};

static const agnc_gateway_descriptor_t agnc_gateway_openrouter = {
    "openrouter",
    "OpenRouter",
    "https://openrouter.ai/api/v1",
    "openrouter/owl-alpha",
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    "Authorization",
    "bearer",
    "/chat/completions",
    "/models",
    1,
    1,
    1,
    agnc_env_openrouter,
    2,
    agnc_models_openrouter,
    1,
};

static const agnc_gateway_descriptor_t *const agnc_gateway_table[] = {
    &agnc_gateway_anthropic,
    &agnc_gateway_custom_openai_compatible,
    &agnc_gateway_gemini,
    &agnc_gateway_naraya,
    &agnc_gateway_ollama,
    &agnc_gateway_opencode_local,
    &agnc_gateway_openrouter,
};

const agnc_gateway_descriptor_t *agnc_registry_gateway_at(size_t index)
{
    if (index >= AGNC_GATEWAY_COUNT) {
        return NULL;
    }
    return agnc_gateway_table[index];
}
