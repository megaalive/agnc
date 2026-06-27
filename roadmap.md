# agnc Roadmap

## 1. Ringkasan Keputusan

`agnc` adalah proyek greenfield untuk membuat CLI pengganti OpenClaude dengan bahasa C. Target produk adalah coding-agent terminal-first yang dapat menggantikan workflow utama OpenClaude: prompt, streaming response, tool calling, provider switching, permission gate, dan mode interaktif.

Porting ini bukan transliterasi file TypeScript ke C. Strategi yang dipakai adalah rewrite bertahap dengan arsitektur yang meniru bentuk internal OpenClaude pada titik yang penting: message model, agent loop, tool registry, provider descriptor, transport layer, config, permission model, dan session lifecycle.

Keputusan yang dikunci:

| Area | Keputusan |
| --- | --- |
| Tujuan produk | CLI pengganti OpenClaude |
| Bahasa | C |
| Platform | Cross-platform, Windows lebih dulu |
| Toolchain | C17 + CMake + Ninja + MSVC/Clang-Cl di Windows |
| Config | Format baru `~/.agnc.json` |
| Legal | Tidak menjadi blocker proyek |
| Internal design | Meniru OpenClaude secara konseptual, bukan copy-paste |
| Provider architecture | Descriptor-first seperti OpenClaude |
| Tool schema | Static C registry + JSON Schema subset |
| Coding language | Nama file, fungsi, variabel, struct, enum, dan error code dalam bahasa Inggris |
| Comment language | Semua komentar kode harus berbahasa Indonesia dan menjelaskan maksud kode dengan baik |

## 2. Scope Produk

### 2.1 Must-Have

Fitur yang wajib ada agar `agnc` layak disebut pengganti CLI OpenClaude:

- `agnc --version`
- `agnc doctor`
- `agnc --print "<prompt>"`
- Mode interaktif `agnc`
- Streaming output dari provider LLM
- Agent loop dengan tool calling multi-step
- Provider OpenAI-compatible sebagai jalur pertama
- Config baru `~/.agnc.json`
- Permission prompt untuk aksi berisiko
- Tool dasar: shell, read, write, edit, grep, glob
- Provider descriptor untuk OpenRouter, Gemini, OpenCode lokal, Naraya, dan custom OpenAI-compatible
- Build Windows lebih dulu, lalu Linux/macOS

### 2.2 Should-Have

Fitur yang penting, tetapi boleh masuk setelah MVP stabil:

- Slash commands subset: `/help`, `/clear`, `/model`, `/provider`, `/doctor`, `/compact`
- Session persistence
- Provider model discovery
- Gemini native transport
- MCP stdio client
- Tool result truncation dan compact sederhana
- Config migration helper untuk versi `~/.agnc.json` berikutnya

### 2.3 Deferred

Fitur yang ditunda sampai core runtime matang:

- Full MCP multi-transport
- OAuth provider flows
- Background sessions
- Sub-agent kompleks
- gRPC server
- Full slash command catalog OpenClaude
- Full provider catalog OpenClaude
- TUI kompleks setara React/Ink
- Hooks dan skills
- Plugin system

### 2.4 Dropped

Tidak dipakai sebagai target port awal:

- React/Ink UI OpenClaude
- VS Code extension
- Web documentation site
- Python helper legacy
- Analytics, GrowthBook, telemetry
- Voice, buddy, proactive, bridge, daemon, dan fitur non-CLI utama

## 3. Prinsip Arsitektur

### 3.1 Mirror OpenClaude Where It Matters

agnc harus meniru OpenClaude pada kontrak internal, bukan pada implementasi literal:

| OpenClaude | agnc |
| --- | --- |
| Anthropic-shaped messages | `agnc_message_t` sebagai internal IR |
| `queryLoop()` | `agnc_query_run()` |
| `Tool` interface | `agnc_tool_descriptor_t` + function pointers |
| Zod schemas | JSON Schema subset dalam string/static struct |
| Provider descriptors | JSON descriptors + generated C registry |
| OpenAI shim | Transport adapter `openai_compat` |
| Commander flags | `agnc_cli_options_t` dari arg parser C |
| React/Ink renderer | Plain terminal renderer, TUI belakangan |
| Settings layers | `~/.agnc.json` v1, atomic write |

### 3.2 Internal Message Model

agnc memakai model pesan internal yang stabil dan tidak bergantung langsung pada format provider. Format internal mengikuti pola Anthropic karena OpenClaude juga memakai bentuk ini sebagai pusat agent loop.

Konsep utama:

- `system` message untuk instruksi sistem
- `user` message untuk input pengguna
- `assistant` message untuk teks dan tool use
- `tool_result` message untuk hasil eksekusi tool
- Content block typed: text, tool_use, tool_result, thinking, error

Sketsa struktur C:

```c
typedef enum {
    AGNC_ROLE_SYSTEM,
    AGNC_ROLE_USER,
    AGNC_ROLE_ASSISTANT,
    AGNC_ROLE_TOOL
} agnc_role_t;

typedef enum {
    AGNC_BLOCK_TEXT,
    AGNC_BLOCK_TOOL_USE,
    AGNC_BLOCK_TOOL_RESULT,
    AGNC_BLOCK_ERROR
} agnc_block_type_t;

typedef struct {
    agnc_block_type_t type;
    char *id;
    char *name;
    char *json;
    char *text;
} agnc_content_block_t;

typedef struct {
    agnc_role_t role;
    agnc_content_block_t *blocks;
    size_t block_count;
} agnc_message_t;
```

Komentar pada kode implementasi harus berbahasa Indonesia, misalnya:

```c
// Simpan format internal yang tidak bergantung pada provider agar adapter HTTP tetap sederhana.
static agnc_status_t agnc_message_append_block(agnc_message_t *message, agnc_content_block_t block);
```

### 3.3 Ownership Memory

C membutuhkan aturan ownership eksplisit sejak awal:

- Semua struct publik memiliki pasangan `init/free`.
- Semua string owned memakai `char *` dan dibebaskan oleh owner struct.
- Semua view non-owned memakai suffix `_view`.
- Semua fungsi yang mengalokasikan memory mengembalikan `agnc_status_t` dan menulis output melalui pointer.
- Tidak ada global mutable state kecuali registry statis read-only.
- Arena allocator boleh ditambahkan untuk satu query turn, tetapi tidak di Fase 0.

Naming:

- Type: `agnc_message_t`
- Function: `agnc_message_init`
- Enum value: `AGNC_STATUS_OK`
- File: `message.c`, `provider_registry.c`

### 3.4 Error Model

Semua modul memakai error code yang konsisten:

```c
typedef enum {
    AGNC_STATUS_OK = 0,
    AGNC_STATUS_INVALID_ARGUMENT,
    AGNC_STATUS_OUT_OF_MEMORY,
    AGNC_STATUS_IO_ERROR,
    AGNC_STATUS_JSON_ERROR,
    AGNC_STATUS_HTTP_ERROR,
    AGNC_STATUS_PROVIDER_ERROR,
    AGNC_STATUS_TOOL_DENIED,
    AGNC_STATUS_TOOL_FAILED,
    AGNC_STATUS_CANCELLED
} agnc_status_t;
```

Setiap error yang ditampilkan ke user harus punya:

- Kode internal
- Pesan ringkas
- Detail opsional untuk mode verbose
- Hint perbaikan jika jelas

## 4. Toolchain Terbaik

### 4.1 Pilihan Utama

| Kebutuhan | Pilihan | Alasan |
| --- | --- | --- |
| Bahasa | C17 | Portable, stabil, cukup modern untuk CLI systems |
| Build | CMake | Cross-platform, umum untuk C |
| Generator | Ninja | Cepat dan konsisten di Windows/Linux/macOS |
| Compiler Windows | MSVC atau Clang-Cl | Integrasi terbaik dengan Windows SDK |
| Compiler Unix | Clang atau GCC | CI dan distribusi mudah |
| Package deps | vcpkg manifest mode | Windows-first, CMake friendly |
| HTTP/TLS | libcurl | Streaming dan HTTPS matang |
| JSON | yyjson | Cepat, ringan, API C bersih |
| CLI args | argtable3 atau parser internal | Scope flag awal kecil |
| Line input | linenoise-ng atau replxx-c wrapper | Lebih ringan dari TUI penuh |
| Terminal UI | ANSI manual dulu, notcurses belakangan | MVP fokus agent loop |
| Process | libuv atau native abstraction | Windows CreateProcess dan POSIX spawn |
| Regex | PCRE2 | Untuk grep fallback |
| Search | Spawn `rg` | Meniru OpenClaude dan cepat |
| Tests | cmocka | Ringan, CMake friendly |
| Sanitizer | ASan/UBSan di Clang/GCC | Memory safety |
| Formatting | clang-format | Konsisten |
| Static analysis | clang-tidy + cppcheck | Deteksi bug C awal |

### 4.2 Rekomendasi Toolchain Windows

Default Windows:

- Visual Studio Build Tools 2022
- CMake
- Ninja
- vcpkg
- MSVC untuk build rilis
- Clang-Cl untuk sanitizer/static analysis jika tersedia

Build command target:

```powershell
cmake --preset x64-Debug
cmake --build --preset x64-Debug
.\out\build\x64-Debug\agnc.exe --version
```

### 4.3 Dependency Policy

- Dependency harus punya alasan jelas dan dipakai lintas modul.
- Hindari dependency yang hanya memecahkan satu kasus kecil.
- Wrapper internal dibuat untuk semua dependency besar agar mudah diganti.
- Tidak boleh menyimpan API key dalam source, test fixture, atau log.

## 5. Struktur Direktori Target

Struktur awal yang disarankan:

```text
agnc/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── roadmap.md
├── .gitignore
├── cmake/
│   ├── Dependencies.cmake
│   └── CompilerWarnings.cmake
├── config/
│   └── agnc.example.json
├── descriptors/
│   ├── gateways/
│   │   ├── openrouter.json
│   │   ├── gemini.json
│   │   ├── opencode-local.json
│   │   ├── naraya.json
│   │   └── custom-openai-compatible.json
│   └── models/
├── generated/
│   ├── agnc_integrations_gen.c
│   └── agnc_integrations_gen.h
├── include/
│   └── agnc/
│       ├── agnc.h
│       ├── status.h
│       ├── message.h
│       ├── config.h
│       ├── tool.h
│       └── provider.h
├── src/
│   ├── main.c
│   ├── cli/
│   │   ├── args.c
│   │   ├── doctor.c
│   │   ├── print.c
│   │   └── repl.c
│   ├── config/
│   │   ├── config.c
│   │   ├── config_schema.c
│   │   └── atomic_write.c
│   ├── engine/
│   │   ├── query.c
│   │   ├── message.c
│   │   ├── conversation.c
│   │   └── tool_orchestrator.c
│   ├── integrations/
│   │   ├── registry.c
│   │   ├── route_metadata.c
│   │   └── runtime_metadata.c
│   ├── net/
│   │   ├── http.c
│   │   └── sse.c
│   ├── providers/
│   │   ├── provider.c
│   │   ├── openai_compat.c
│   │   └── gemini.c
│   ├── permissions/
│   │   └── permissions.c
│   ├── tools/
│   │   ├── registry.c
│   │   ├── shell.c
│   │   ├── read_file.c
│   │   ├── write_file.c
│   │   ├── edit_file.c
│   │   ├── grep.c
│   │   └── glob.c
│   └── util/
│       ├── log.c
│       ├── path.c
│       ├── string.c
│       ├── process_win32.c
│       └── process_posix.c
├── scripts/
│   ├── generate_integrations.py
│   └── run_spike.ps1
└── tests/
    ├── CMakeLists.txt
    ├── test_config.c
    ├── test_sse.c
    ├── test_message.c
    ├── test_tool_schema.c
    └── fixtures/
```

## 6. Config Baru `~/.agnc.json`

### 6.1 Prinsip

- Tidak kompatibel wajib dengan OpenClaude.
- Mudah dibaca manusia.
- Secret tidak wajib disimpan inline.
- Mendukung Windows path.
- Wajib atomic write untuk mencegah config corrupt.
- Schema version wajib ada sejak v1.

### 6.2 Format V1

```json
{
  "schema_version": 1,
  "provider": {
    "active": "openrouter",
    "model": "anthropic/claude-sonnet-4",
    "base_url": "https://openrouter.ai/api/v1",
    "api_key_env": "AGNC_API_KEY",
    "api_key_file": null
  },
  "providers": {
    "openrouter": {
      "gateway": "openrouter",
      "base_url": "https://openrouter.ai/api/v1",
      "api_key_env": "OPENROUTER_API_KEY",
      "default_model": "anthropic/claude-sonnet-4"
    },
    "opencode-local": {
      "gateway": "opencode-local",
      "base_url": "http://127.0.0.1:4096/v1",
      "api_key_env": null,
      "default_model": "local-default"
    }
  },
  "permissions": {
    "mode": "default",
    "always_allow": [],
    "always_deny": [],
    "always_ask": ["shell"]
  },
  "tools": {
    "enabled": ["shell", "read_file", "write_file", "edit_file", "grep", "glob"],
    "shell": {
      "windows_shell": "powershell",
      "unix_shell": "sh",
      "timeout_ms": 30000
    }
  },
  "runtime": {
    "max_tool_iterations": 25,
    "stream": true,
    "verbose": false
  },
  "paths": {
    "sessions_dir": "~/.agnc/sessions",
    "cache_dir": "~/.agnc/cache"
  }
}
```

### 6.3 Config Resolution

Urutan resolusi:

1. CLI flags
2. Environment variables
3. `~/.agnc.json`
4. Descriptor defaults
5. Built-in defaults

Environment awal:

- `AGNC_CONFIG`
- `AGNC_API_KEY`
- `AGNC_BASE_URL`
- `AGNC_MODEL`
- `AGNC_PROVIDER`
- Provider-specific env seperti `OPENROUTER_API_KEY`, `GEMINI_API_KEY`

## 7. Provider dan Integrations Descriptor

### 7.1 Prinsip Descriptor-First

agnc harus mengikuti prinsip OpenClaude:

- Descriptor mendefinisikan apa provider/gateway itu.
- Routing memilih descriptor aktif dari config/env.
- Transport mengeksekusi request berdasarkan descriptor.
- Runtime metadata mengubah request provider tanpa switch besar tersebar di kode.

### 7.2 Descriptor Source

Descriptor ditulis sebagai JSON agar mudah dibaca dan bisa digenerate ke C.

Contoh `descriptors/gateways/openrouter.json`:

```json
{
  "id": "openrouter",
  "label": "OpenRouter",
  "category": "aggregating",
  "default_base_url": "https://openrouter.ai/api/v1",
  "default_model": "anthropic/claude-sonnet-4",
  "setup": {
    "requires_auth": true,
    "auth_mode": "api-key",
    "credential_env_vars": ["OPENROUTER_API_KEY", "AGNC_API_KEY"]
  },
  "transport": {
    "kind": "openai-compatible",
    "auth_header": {
      "name": "Authorization",
      "scheme": "bearer"
    },
    "endpoint_path": "/chat/completions",
    "supports_streaming": true,
    "supports_tool_calls": true,
    "max_tokens_field": "max_tokens"
  },
  "catalog": {
    "source": "static",
    "models": [
      {
        "id": "claude-sonnet-4",
        "api_name": "anthropic/claude-sonnet-4",
        "capabilities": {
          "streaming": true,
          "tool_calls": true,
          "reasoning": false
        }
      }
    ]
  }
}
```

### 7.3 Generated C Registry

`scripts/generate_integrations.py` menghasilkan:

- `generated/agnc_integrations_gen.c`
- `generated/agnc_integrations_gen.h`

Sketsa tipe:

```c
typedef enum {
    AGNC_TRANSPORT_OPENAI_COMPATIBLE,
    AGNC_TRANSPORT_GEMINI_NATIVE,
    AGNC_TRANSPORT_LOCAL
} agnc_transport_kind_t;

typedef struct {
    const char *id;
    const char *api_name;
    bool supports_streaming;
    bool supports_tool_calls;
    bool supports_reasoning;
} agnc_model_descriptor_t;

typedef struct {
    const char *id;
    const char *label;
    const char *default_base_url;
    const char *default_model;
    agnc_transport_kind_t transport_kind;
    const char *auth_header_name;
    const char *auth_header_scheme;
    const agnc_model_descriptor_t *models;
    size_t model_count;
} agnc_gateway_descriptor_t;
```

Komentar implementasi harus seperti:

```c
// Descriptor dibuat read-only agar registry bisa dipakai tanpa alokasi saat startup.
const agnc_gateway_descriptor_t *agnc_registry_find_gateway(const char *id);
```

### 7.4 Provider Roadmap

| Provider | Fase | Transport | Catatan |
| --- | --- | --- | --- |
| OpenRouter | Spike/Fase 1 | OpenAI-compatible | Target utama pertama |
| Custom OpenAI-compatible | Fase 1 | OpenAI-compatible | Untuk endpoint lokal/remote generik |
| OpenCode lokal | Fase 1/2 | Local/OpenAI-compatible | Base URL lokal |
| Naraya | Fase 2 | OpenAI-compatible jika memungkinkan | Descriptor dibuat setelah endpoint dikunci |
| Gemini | Fase 3 | Gemini native | Bisa lewat OpenAI-compatible lebih dulu jika tersedia |
| Anthropic native | Fase 4 | Anthropic native | Setelah internal IR stabil |
| Ollama | Fase 4 | Local | Berguna untuk offline coding |

## 8. Tool System dan Tool Schema

### 8.1 Tool Registry

Setiap tool adalah descriptor statis dengan function pointers:

```c
typedef struct agnc_tool_context agnc_tool_context_t;

typedef agnc_status_t (*agnc_tool_validate_fn)(const char *input_json, char **error_message);
typedef agnc_status_t (*agnc_tool_execute_fn)(agnc_tool_context_t *ctx, const char *input_json, char **result_json);
typedef agnc_status_t (*agnc_tool_permission_fn)(agnc_tool_context_t *ctx, const char *input_json);

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;
    agnc_tool_validate_fn validate;
    agnc_tool_permission_fn check_permission;
    agnc_tool_execute_fn execute;
} agnc_tool_descriptor_t;
```

Komentar kode:

```c
// Validasi dilakukan sebelum permission prompt agar input rusak tidak pernah dieksekusi.
static agnc_status_t shell_validate(const char *input_json, char **error_message);
```

### 8.2 JSON Schema Subset

MVP hanya mendukung subset:

- `type: object`
- `properties`
- `required`
- `type: string`
- `type: boolean`
- `type: integer`
- `enum`
- `description`

Tidak wajib di MVP:

- nested arbitrary schema
- oneOf/anyOf/allOf
- patternProperties
- JSON Schema draft penuh

Tool schema disimpan sebagai JSON string agar langsung bisa dikirim ke provider OpenAI-compatible.

### 8.3 MVP Tool List

| Tool | Nama internal | Fase | Permission |
| --- | --- | --- | --- |
| Shell | `shell` | Spike/Fase 1 | Ask default |
| Read file | `read_file` | Spike/Fase 1 | Allow dalam workspace |
| Write file | `write_file` | Fase 1 | Ask |
| Edit file | `edit_file` | Fase 1 | Ask |
| Grep | `grep` | Fase 2 | Allow |
| Glob | `glob` | Fase 2 | Allow |
| Todo | `todo_write` | Fase 3 | Allow |
| Web fetch | `web_fetch` | Fase 4 | Ask/Allow configurable |
| MCP tool | `mcp_call` | Fase 5 | Ask |

### 8.4 Windows Shell Strategy

Karena Windows adalah target pertama:

- Default shell: PowerShell.
- Command execution memakai `CreateProcessW`.
- Semua path internal disimpan UTF-8, lalu dikonversi ke UTF-16 saat memanggil Win32 API.
- Timeout wajib.
- Output stdout/stderr harus dibatasi ukuran.
- Dangerous command classifier sederhana dibuat sebelum integrasi permission lanjutan.

## 9. Agent Loop

### 9.1 Loop MVP

Alur dasar:

1. Load config.
2. Resolve provider descriptor.
3. Build system prompt.
4. Build message array.
5. Convert internal messages ke provider request.
6. Stream response.
7. Jika ada tool call, validasi input.
8. Check permission.
9. Execute tool.
10. Append tool result ke messages.
11. Ulangi sampai final text atau max iteration.

Pseudo-code:

```c
agnc_status_t agnc_query_run(agnc_query_context_t *ctx, const char *prompt)
{
    // Loop dibatasi agar model yang terus meminta tool tidak membuat proses menggantung.
    for (size_t i = 0; i < ctx->max_tool_iterations; i++) {
        agnc_provider_response_t response;
        agnc_status_t status = agnc_provider_stream(ctx, &response);
        if (status != AGNC_STATUS_OK) {
            return status;
        }

        if (!response.has_tool_calls) {
            return AGNC_STATUS_OK;
        }

        status = agnc_tool_orchestrator_run(ctx, &response);
        if (status != AGNC_STATUS_OK) {
            return status;
        }
    }

    return AGNC_STATUS_PROVIDER_ERROR;
}
```

### 9.2 Streaming Contract

Internal streaming event:

```c
typedef enum {
    AGNC_STREAM_TEXT_DELTA,
    AGNC_STREAM_TOOL_CALL_START,
    AGNC_STREAM_TOOL_CALL_DELTA,
    AGNC_STREAM_TOOL_CALL_DONE,
    AGNC_STREAM_USAGE,
    AGNC_STREAM_DONE,
    AGNC_STREAM_ERROR
} agnc_stream_event_type_t;
```

Provider adapter bertugas mengubah SSE/JSON provider menjadi event internal ini.

## 10. Technical Spike

### 10.1 Tujuan Spike

Membuktikan bahwa agnc bisa melakukan satu agent turn nyata di Windows dengan C:

- Build `agnc.exe`
- Load config minimal
- Kirim request streaming ke OpenRouter
- Terima SSE
- Parse tool call
- Execute satu tool
- Kirim tool result
- Cetak final answer

### 10.2 Scope Spike

Tool spike: `read_file`.

Provider spike: OpenRouter OpenAI-compatible.

Mode spike: `agnc --print`.

Tidak termasuk:

- TUI
- Session persistence
- MCP
- Multiple providers
- Full permission system
- File write/edit

### 10.3 Spike Command Target

```powershell
$env:OPENROUTER_API_KEY = "..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.\out\build\x64-Debug\agnc.exe --print "Read roadmap.md and summarize the product goal."
```

### 10.4 Spike Deliverables

- `CMakeLists.txt`
- `src/main.c`
- `src/config/config.c`
- `src/net/http.c`
- `src/net/sse.c`
- `src/providers/openai_compat.c`
- `src/engine/query.c`
- `src/tools/read_file.c`
- `tests/test_sse.c`
- `config/agnc.example.json`

### 10.5 Spike Success Criteria

Spike dianggap sukses jika:

- Build berhasil di Windows.
- `agnc --version` berjalan.
- `agnc doctor` minimal memeriksa config, curl, dan provider key.
- `agnc --print` bisa streaming token dari OpenRouter.
- Model bisa meminta `read_file`.
- agnc mengeksekusi `read_file`.
- agnc mengirim tool result ke provider.
- Final assistant answer tercetak.
- Missing API key menghasilkan error yang jelas.
- Tidak ada secret tercetak di log.
- `test_sse` lulus untuk fixture chunked SSE.

## 11. Acceptance Criteria

### 11.1 Fase 0 Acceptance

- Repo punya `.gitignore` yang mengecualikan `.keys/`, `build/`, `.env`, binary, object files.
- CMake project bisa generate dan build di Windows.
- `agnc --version` menampilkan versi.
- `agnc doctor` menampilkan OS, compiler, config path, dan status dependency dasar.
- CI minimal dapat menjalankan configure/build/test di Windows.

### 11.2 Fase 1 Acceptance

Status: **selesai** (2026-06).

- `~/.agnc.json` berhasil dibaca dan divalidasi.
- Config write memakai atomic write.
- OpenRouter OpenAI-compatible request berhasil.
- SSE parser stabil untuk chunk parsial.
- `agnc --print` mendukung jawaban utuh per turn (non-stream by default).
- Agent loop mendukung tool round-trip multi-step.
- Tool `read_file` dan `shell` tersedia.
- Permission prompt muncul untuk `shell`; `--yes` untuk non-interaktif.
- Error provider ditampilkan dengan pesan jelas.
- Renderer markdown/tabel ASCII di stdout.

### 11.3 Fase 2 Acceptance

Status: **selesai** (Windows-first, 2026-06).

- Tools `write_file`, `edit_file`, `grep`, dan `glob` tersedia.
- Path traversal (`..`) dan akses di luar workspace ditolak.
- Shell tool punya batas output; write/edit memakai atomic write.
- `agnc doctor` memeriksa `rg`.
- Unit tests: SSE, shell, markdown, atomic write, tool path/write/edit.
- CI Windows build + ctest + doctor.

### 11.4 Fase 3 Acceptance

Status: **selesai** (Windows-first, 2026-06).

- Descriptor generator menghasilkan C registry deterministik (`scripts/generate_integrations.py`).
- Provider OpenRouter, custom OpenAI-compatible, OpenCode lokal, Naraya, dan Gemini tercatat di `descriptors/gateways/`.
- Provider aktif bisa dipilih dari config (`provider.active`, `providers{}`, env `AGNC_PROVIDER`).
- Model discovery tersedia untuk provider OpenAI-compatible (`agnc_provider_list_models`, GET `/models`).
- `agnc doctor` menampilkan registry dan provider aktif.
- Unit tests: `test_provider_registry`, `test_config_provider`.

*(Slash `/provider` di mode interaktif masuk Fase 4.)*

### 11.5 Fase 4 Acceptance

Status: **selesai** (Windows-first, 2026-06).

- Mode interaktif default (`agnc` tanpa argumen) — REPL di `src/cli/repl.c`.
- Streaming; render markdown sekali di akhir turn (bukan live duplikat).
- Slash commands: `/help`, `/clear`, `/model`, `/provider`, `/doctor`, `/compact`, `/exit`.
- Session tunggal: `~/.agnc/sessions/current.json` (`agnc_session_save` / `load`).
- Bersihkan `current.json.tmp*` stale saat REPL startup (`agnc_session_cleanup_stale_temp_files`).
- Auto-compact riwayat saat mendekati batas 64 pesan; truncate hasil tool besar.
- System prompt menyertakan workspace root; diperbarui tiap request.
- Permission/tool log di REPL ke stdout; spinner hanya saat tunggu HTTP.
- Warna REPL: user hijau, inline code abu-abu, pesan sistem dim.
- Ctrl+C membatalkan request HTTP aktif (`AGNC_STATUS_CANCELLED`).
- Unit test: `test_session`, `test_markdown_render`.

### 11.6 Fase 5 Acceptance

Status: **selesai** (2026-06).

- MCP stdio client bisa list tools dan call tool.
- MCP tool schema masuk ke registry runtime.
- Permission gate berlaku untuk MCP.
- Error MCP tidak membuat query loop crash.
- E2E filesystem nyata: `test_mcp_filesystem_e2e`, `agnc doctor` (`mcp_connect`), `agnc --print` dengan `mcp_workspace-fs_*`.

### 11.7 Utang terbuka (bukan blocker Fase 5)

| Item | Fase roadmap | Status | Rencana |
| --- | --- | --- | --- |
| Line editing REPL (`fgets`) | 4 task | Belum | **Fase 6.2** (prioritas) — lihat §12.0 |
| `todo_write` tool | 3 §8.3 | Belum | Fase 6 backlog / slot kecil |
| `web_fetch` tool | 4 §8.3 | Belum | **Fase 6.2** (prioritas) — lihat §12.0 |
| `ripgrep` di PATH dev | 2 doctor | Lingkungan | Pasang `rg` di mesin dev; `grep` tool butuh binary |

Progress Fase 5: **B1–B5** selesai; **B6** housekeeping + celah terdokumentasi — berikutnya (lihat §12.0).

### 11.8 Celah implementasi (ditunda ke B6 / Fase 6.1)

Ditemukan saat E2E Fase 5; bukan regresi acceptance, tapi mengganggu pemakaian harian:

| Celah | Dampak | Target |
| --- | --- | --- |
| `permissions.always_allow` belum diparse | Entri `always_allow: ["mcp"]` di config tidak berpengaruh | B6 / Fase 6.1 |
| `mcp.servers[].env` belum dipakai saat spawn | Server yang butuh env custom gagal diam-diam | B6 backlog |
| MCP reconnect tiap `agnc_query_run` | REPL lambat (terutama cold `npx`) | Fase 6.1 |
| `--yes` tidak mencakup permission MCP | Headless/script harus pipe `y` manual | Fase 6.1 |
| UTF-8 BOM di config | `config_load` gagal jika editor menulis BOM | **Selesai** — strip di `config.c` |

## 12. Roadmap Fase

### 12.0 Urutan kerja pasca-Fase 5 (dikunci 2026-06)

Urutan praktis sebelum fitur besar; jangan loncat ke sub-agent/OAuth/gRPC sebelum tiga langkah awal stabil.

| Langkah | Isi | Exit singkat |
| --- | --- | --- |
| **1. Housekeeping Fase 5** | Tandai §11.6 selesai; catat celah §11.8; rapikan milestone B6 di bawah | Roadmap selaras dengan kode; tidak ada acceptance Fase 5 yang menggantung |
| **2. Stabilisasi MCP harian** (Fase 6.1) | Persist koneksi MCP per sesi REPL; parse `always_allow`; `--yes` untuk MCP | REPL kedua prompt tidak spawn `npx` ulang; `always_allow: ["mcp"]` dan `--yes` menekan prompt MCP |
| **3. Fase 6.2 — dua fitur** | Line editing REPL + `web_fetch` | REPL: backspace/history dasar; `web_fetch` dengan permission + test |
| **4. Fitur besar** (Fase 6.3+) | Sub-agent, OAuth, Anthropic native, gRPC, hooks, skills, TUI, cost tracking | Per item: milestone + acceptance sendiri sebelum mulai kode |

**Prioritas Fase 6.2 (dikunci):** line editing REPL, lalu `web_fetch`. `todo_write` dan item §11.7 lainnya masuk backlog 6.3+.

### Fase 0: Bootstrap Repository (1-2 minggu)

Tujuan: fondasi build dan struktur proyek.

Tasks:

- Inisialisasi git.
- Buat `.gitignore`.
- Buat CMake skeleton.
- Buat `agnc --version`.
- Buat `agnc doctor` minimal.
- Integrasi vcpkg manifest.
- Tambahkan yyjson, libcurl, cmocka.
- Tambahkan CI Windows.
- Tambahkan `config/agnc.example.json`.

Exit criteria: Fase 0 Acceptance terpenuhi.

### Fase 1: Spike dan MVP Headless (4-8 minggu)

Tujuan: satu agent turn nyata lewat OpenRouter.

Tasks:

- Implement config loader.
- Implement HTTP POST streaming dengan libcurl.
- Implement SSE parser.
- Implement OpenAI-compatible provider adapter.
- Implement internal message model.
- Implement `read_file`.
- Implement `shell` versi Windows.
- Implement basic permission prompt.
- Implement `agnc --print`.
- Tambahkan tests untuk SSE dan schema.

Exit criteria: Fase 1 Acceptance terpenuhi.

### Fase 2: Core Tools dan Safety (4-6 minggu)

Tujuan: agent coding minimal bisa membaca, menulis, mengedit, dan mencari file.

Tasks:

- Implement `write_file`.
- Implement `edit_file`.
- Implement `grep` via `rg`.
- Implement `glob`.
- Implement path normalization UTF-8/UTF-16.
- Implement output truncation.
- Implement permission rule dari config.
- Tambahkan tests tool validation.

Exit criteria: Fase 2 Acceptance terpenuhi.

### Fase 3: Provider Descriptor System (4-8 minggu)

Tujuan: provider tidak hard-coded.

Tasks:

- Buat descriptor JSON schema.
- Buat generator `scripts/generate_integrations.py`.
- Generate C registry.
- Tambahkan descriptor OpenRouter, custom OpenAI-compatible, OpenCode lokal, Naraya.
- Tambahkan Gemini descriptor.
- Implement provider resolution.
- Implement model discovery untuk OpenAI-compatible `/models`.

Exit criteria: Fase 3 Acceptance terpenuhi.

### Fase 4: Interactive CLI (6-10 minggu)

Tujuan: menggantikan workflow terminal OpenClaude dasar.

Tasks:

- Implement REPL sederhana.
- Tambahkan line editing.
- Tambahkan streaming renderer.
- Tambahkan slash commands subset.
- Tambahkan session persistence.
- Tambahkan compact sederhana.
- Tambahkan Ctrl+C cancellation.

Exit criteria: Fase 4 Acceptance terpenuhi.

### Fase 5: MCP Stdio (6-10 minggu)

Tujuan: membuka integrasi tool eksternal.

**Keputusan dikunci (2026-06):**

| Topik | Keputusan |
| --- | --- |
| Server dev pertama | **Filesystem lokal** — `@modelcontextprotocol/server-filesystem` via stdio |
| Debt Fase 4 | **Tunda** `web_fetch` dan line editing REPL |
| Config | **Multi-server** dari hari pertama (`mcp.servers[]`); implementasi bertahap per server |

**Skema config (v1):**

```json
"mcp": {
  "servers": [
    {
      "id": "workspace-fs",
      "enabled": true,
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "D:/path/to/workspace"],
      "cwd": null,
      "env": {}
    }
  ]
}
```

Tool MCP diekspos ke model dengan prefix `mcp_<id>_<tool>` (mis. `mcp_workspace-fs_read_file`).

Tasks:

- Implement JSON-RPC client.
- Implement MCP stdio transport (spawn + newline-framed JSON).
- Handshake `initialize` / `notifications/initialized`.
- `tools/list` dan `tools/call` per server aktif.
- Convert MCP `inputSchema` ke OpenAI tool schema (subset §8.2).
- Gabungkan MCP tools ke registry runtime di `query.c`.
- Permission gate MCP (`always_ask` + prompt REPL).
- Server dev: filesystem lokal; uji multi-server dengan ≥2 entri config (satu disabled).

**Milestone implementasi:**

| # | Deliverable | File utama |
| --- | --- | --- |
| B1 | JSON-RPC parse/serialize + test fixture | `src/mcp/jsonrpc.c`, `tests/test_mcp_jsonrpc.c` |
| B2 | Stdio transport + spawn proses Windows | `src/mcp/stdio.c` |
| B3 | MCP session (initialize, tools/list, tools/call) | `src/mcp/client.c`, mock server di `tests/fixtures/` |
| B4 | Config loader `mcp.servers[]` + multi-server manager | `src/config/config.c`, `src/mcp/registry.c` |
| B5 | Wire agent loop + permission + doctor | `query.c`, `permissions.c`, `doctor.c` |
| B6 | Housekeeping: §11.6/§11.8, perbaikan kecil dokumentasi & celah opsional (`env` spawn) | `roadmap.md`, `config.c`, `permissions.c` |

Exit criteria inti: Fase 5 Acceptance (B1–B5) — **terpenuhi**. B6 opsional sebelum Fase 6.1.

### Fase 6: Parity Lanjut (ongoing)

Tujuan: pemakaian harian nyaman, lalu mendekati pengalaman agent IDE penuh.

Ikuti urutan §12.0. Jangan mulai **6.3+** sebelum **6.1** dan **6.2** selesai.

#### Fase 6.1 — Stabilisasi MCP harian

Tasks:

- Simpan `agnc_mcp_registry_t` + katalog tool di lifetime sesi REPL (bukan reconnect tiap `agnc_query_run`).
- Parse `permissions.always_allow` (subset: `mcp`, `shell`, `write_file`, `edit_file`) — hormati bersama `always_ask`.
- Perluas `--yes` / `auto_approve` agar mencakup prompt MCP.
- (Opsional B6) Terapkan `mcp.servers[].env` saat `CreateProcess`.

Acceptance:

- Dua prompt berturut di REPL memakai satu proses MCP per server (verifikasi via log verbose atau test).
- Config `always_allow: ["mcp"]` menekan prompt MCP; `--yes` setara untuk headless.
- `ctest` MCP tetap lulus; tidak ada regresi `test_mcp_filesystem_e2e`.

#### Fase 6.2 — REPL + web (prioritas)

Tasks:

- Ganti `fgets` dengan line editing minimal (backspace, panjang baris, history ringkas opsional).
- Implement `web_fetch` tool (HTTP GET, truncation, permission ask/allow).

Acceptance:

- REPL: edit baris sebelum Enter; tidak perlu eksternal readline untuk alur dasar.
- `web_fetch` bisa mengambil URL HTTPS dan mengembalikan teks ke model; ditolak/diizinkan sesuai config.
- Unit atau integration test untuk `web_fetch`; smoke manual di README §13.3.

#### Fase 6.3+ — Backlog fitur besar

Candidates (masing-masing butuh milestone + acceptance sebelum implementasi):

- Background sessions
- Sub-agent
- OAuth
- Anthropic native
- Ollama/local model polish
- gRPC server
- Hooks
- Skills
- TUI lebih kaya
- Token usage dan cost tracking
- `todo_write` tool (§11.7)

## 13. Testing Strategy

### 13.1 Unit Tests

Minimal test suite:

- `test_config.c`
- `test_sse.c`
- `test_message.c`
- `test_tool_schema.c`
- `test_permissions.c`
- `test_provider_openai_compat.c`

### 13.2 Integration Tests

Tests yang boleh memakai fixture offline:

- SSE stream chunked
- Tool call JSON parse
- Tool result serialization
- Descriptor generation determinism
- Config atomic write recovery

Tests yang butuh provider nyata harus opt-in:

- `AGNC_TEST_LIVE_OPENROUTER=1`
- `AGNC_TEST_LIVE_GEMINI=1`

### 13.3 Manual Smoke Tests

```powershell
.\out\build\x64-Debug\agnc.exe --version
.\out\build\x64-Debug\agnc.exe doctor
.\out\build\x64-Debug\agnc.exe --print "Say hello in one sentence."
.\out\build\x64-Debug\agnc.exe --print "Read roadmap.md and summarize it."
```

## 14. Coding Rules

### 14.1 Bahasa Kode

Semua identifier harus bahasa Inggris:

- File name
- Function name
- Struct name
- Enum name
- Variable name
- Constant name
- Error code
- Test name

Contoh benar:

```c
// Pastikan konfigurasi sudah divalidasi sebelum provider dipanggil.
agnc_status_t agnc_config_load(const char *path, agnc_config_t *config);
```

Contoh salah:

```c
// Jangan pakai nama fungsi bahasa Indonesia.
agnc_status_t muat_konfigurasi(const char *path, agnc_config_t *config);
```

### 14.2 Bahasa Komentar

Semua komentar kode wajib bahasa Indonesia.

Komentar harus menjelaskan alasan, batasan, atau alur yang tidak jelas. Hindari komentar yang hanya mengulang nama fungsi.

Komentar baik:

```c
// Gunakan file sementara lalu rename atomik agar config tidak rusak saat proses mati mendadak.
static agnc_status_t write_config_atomically(const char *path, const char *json);
```

Komentar buruk:

```c
// Menulis config.
static agnc_status_t write_config_atomically(const char *path, const char *json);
```

### 14.3 Memory Safety

- Setiap allocation harus punya owner.
- `free` dilakukan di fungsi `*_free`.
- Tidak boleh return pointer ke stack.
- Tidak boleh menyimpan borrowed pointer dalam struct owned tanpa dokumentasi.
- Test dengan ASan jika toolchain mendukung.

### 14.4 Security

- Secret tidak boleh masuk log.
- `.keys/` tidak boleh di-commit.
- Shell command harus melewati permission gate.
- File write/edit harus melewati permission gate.
- Path harus dinormalisasi sebelum permission check.
- Output tool harus dibatasi ukuran.

## 15. Risiko dan Mitigasi

| Risiko | Dampak | Mitigasi |
| --- | --- | --- |
| Scope parity terlalu besar | Proyek melambat | Fase ketat, MVP headless dulu |
| SSE/tool call parsing rumit | Agent loop tidak stabil | Spike Fase 1 sebelum tools lain |
| Windows process handling | Shell tool rawan bug | Abstraksi `process_win32.c`, test manual |
| Memory safety | Crash/security issue | Ownership rule, ASan, tests |
| Provider beda format | Banyak adapter khusus | Internal IR + descriptor runtime metadata |
| Config corrupt | CLI tidak bisa start | Atomic write dan backup |
| Secret leak | Risiko keamanan | Redaction di logger dan doctor |
| TUI makan waktu | Delay MVP | ANSI/repl sederhana dulu |
| MCP kompleks | Scope creep | stdio-only setelah core stabil |

## 16. First Implementation Order

Urutan kerja yang direkomendasikan:

1. Buat `.gitignore` dan pastikan `.keys/` ignored.
2. Buat CMake skeleton.
3. Buat `agnc --version`.
4. Buat `agnc doctor` minimal.
5. Tambahkan yyjson dan libcurl.
6. Buat `~/.agnc.json` loader.
7. Buat SSE parser dengan fixture test.
8. Buat OpenRouter streaming request.
9. Buat internal message model.
10. Buat `read_file` tool.
11. Buat satu tool loop end-to-end.
12. Tambahkan `shell` dengan permission prompt.
13. Baru lanjut ke write/edit/grep/glob.

## 17. Definition of Ready untuk Coding

Sebuah task siap dikerjakan jika:

- Fase dan acceptance criteria jelas.
- File target jelas.
- Input/output fungsi utama jelas.
- Error behavior jelas.
- Test minimal jelas.
- Risiko permission/security sudah disebut.

## 18. Definition of Done

Sebuah task selesai jika:

- Build lulus di Windows.
- Test terkait lulus.
- Tidak ada secret di log/test.
- Error path diuji.
- Komentar kode yang ditambahkan berbahasa Indonesia.
- README/roadmap diupdate jika behavior user-facing berubah.

## 19. Kesimpulan

Rencana matang untuk mulai eksekusi dari Fase 0 dan Spike Fase 1. Kunci keberhasilan adalah menjaga scope tetap sempit sampai agent loop headless benar-benar bekerja. Setelah `agnc --print` bisa melakukan streaming dan satu tool round-trip via OpenRouter, barulah tools lain, descriptor generator, REPL, dan MCP ditambahkan bertahap.

