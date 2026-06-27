# agnc

CLI coding-agent untuk pemakaian sehari-hari di terminal — chat, tool, dan query ke model LLM.

Ditulis dalam C, Windows-first.

## Visual Studio 2026 (Disarankan untuk Debug)

1. Buka folder proyek ini di Visual Studio 2026 (**Open a local folder**).
2. Pilih preset **x64 Debug**.
3. Build dengan **Ctrl+Shift+B**.
4. Pilih profil debug di `launch.vs.json` (mis. `agnc doctor`) lalu tekan **F5**.

Binary: `out/build/x64-Debug/agnc.exe`

Panduan lengkap (termasuk troubleshooting yyjson, analyzer MSVC, IntelliSense): [docs/visual-studio-2026.md](docs/visual-studio-2026.md)

## Build (Windows)

Satu lokasi output untuk semua workflow: **`out/build/x64-Debug/agnc.exe`** (sama dengan Visual Studio).

### Opsi A: Script build (disarankan)

```powershell
.\scripts\build.ps1          # Debug (default)
.\scripts\build.ps1 release  # Release
.\out\build\x64-Debug\agnc.exe --version
.\out\build\x64-Debug\agnc.exe doctor
```

Script ini memakai **CMake preset** dan **MSVC** yang sama dengan Visual Studio 2026.

### Opsi B: CMake Presets manual

Butuh VsDevShell atau environment MSVC aktif:

```powershell
cmake --preset x64-Debug
cmake --build --preset x64-Debug
.\out\build\x64-Debug\agnc.exe --version
.\out\build\x64-Debug\agnc.exe doctor
```

## Perintah CLI

### Setup config

Salin config dan pastikan API key tersedia (env atau `.keys/openrouter.txt`):

```powershell
copy config\agnc.example.json $env:USERPROFILE\.agnc.json
# set env: $env:AGNC_API_KEY = "sk-..."
```

Provider aktif diatur lewat `provider.active` dan entri `providers` di `~/.agnc.json` (lihat `config/agnc.example.json`). Gateway dideskripsikan di `descriptors/gateways/*.json` dan dikompilasi ke registry C lewat:

```powershell
python scripts/generate_integrations.py   # juga dijalankan otomatis oleh build.ps1
```

Override env: `AGNC_PROVIDER`, `AGNC_BASE_URL`, `AGNC_MODEL`.

### Mode interaktif (default)

Jalankan tanpa argumen untuk REPL chat dengan streaming:

```powershell
.\out\build\x64-Debug\agnc.exe
```

Slash commands: `/help`, `/clear`, `/compact`, `/model`, `/models`, `/provider`, `/mcp`, `/session`, `/doctor`, `/exit`.

**Line editing (Windows):** cursor, Backspace/Delete, Home/End, history 32 baris (panah atas/bawah), paste dari clipboard, dan wrap multi-baris — lewat `agnc_repl_read_line` (`src/cli/line_edit.c`) dan sesi input konsol (`src/cli/console.c`). Di Unix fallback ke `fgets` dengan history.

**Tampilan REPL:** blok chat ber-timestamp, warna (user hijau, kode abu-abu, sistem dim), spinner saat menunggu model, log aktivitas tool, prompt izin tool terintegrasi.

**Session:** multi-sesi SQLite (`<nama>.sqlite`); lazy load 48 pesan terakhir ke RAM; sync append-only per turn; context LLM windowed (32 pesan + ringkasan). `/session`, `/session delete`, `/compact` selaraskan storage. Migrasi `.json` legacy otomatis.

**Skills:** file `.md` atau `*/SKILL.md` di `~/.agnc/skills` dan `.agnc/skills` dimuat ke system prompt. Konfigurasi `skills.enabled` / `skills.paths` di `~/.agnc.json`. REPL: `/skills`, `/skills reload`.

**Hooks:** perintah shell per event (`session_start`, `pre_turn`, `post_turn`, `pre_tool`, `post_tool`) di `hooks` config. Script membaca payload JSON dari env `AGNC_HOOK_PAYLOAD_FILE`. `pre_tool` dengan exit ≠ 0 memblokir tool. REPL: `/hooks`.

**Ollama (lokal):** set `provider.active` ke `ollama` (atau `/provider ollama`) dengan `base_url` `http://127.0.0.1:11434/v1`. Tidak perlu API key lokal. `agnc doctor` memeriksa Ollama.

**OpenCode (lokal):** jalankan `opencode serve` (default `http://127.0.0.1:4096`). Set `provider.active` ke `opencode` — transport native (bukan OpenAI-compat). Auth opsional lewat env `OPENCODE_SERVER_PASSWORD`. Session OpenCode di-link otomatis per sesi SQLite agnc.

**Model:** `/model` tanpa argumen menampilkan model aktif; `/model <id>` mengganti model. Discovery semua provider: `agnc models [provider] [filter]` atau REPL `/models` (substring filter, case-insensitive). Ctrl+C membatalkan request chat maupun discovery. Menjawab `y` pada prompt permission mengizinkan kategori tool tersebut untuk sisa sesi REPL (shell, tulis/edit, MCP, web fetch).

Setelah setiap turn berhasil, REPL menampilkan ringkasan token usage jika provider mengirimkannya (`token: turn N · sesi M`). `/usage` menampilkan total sesi; total disimpan di meta SQLite sesi. `/mcp` menampilkan status server MCP; `/mcp reconnect` memuat ulang koneksi.

### Mode headless `--print`

```powershell
# Chat tanpa tool
.\out\build\x64-Debug\agnc.exe --print --no-tools "Say hello."

# Agent dengan tool (read, write, edit, grep, glob, shell)
.\out\build\x64-Debug\agnc.exe --print "Read README.md and summarize it."

# Shell non-interaktif (skip prompt permission)
.\out\build\x64-Debug\agnc.exe --print --yes "gunakan shell: dir"

# Agent dengan MCP (butuh mcp.servers di ~/.agnc.json)
.\out\build\x64-Debug\agnc.exe --print --yes "list files in the repo root via MCP"
```

| Flag | Keterangan |
| --- | --- |
| *(tanpa argumen)* | REPL interaktif dengan streaming |
| `--print "prompt"` | Query headless ke provider |
| `--no-tools` | Chat tanpa tool schema |
| `--yes` / `-y` | Setujui otomatis: shell, tulis/edit file, MCP, `web_fetch` |
| `doctor` | Cek config, libcurl, yyjson, ripgrep, ctags, koneksi MCP |
| `models [provider] [filter]` | Discovery model semua provider config (`--json`, `--filter`) |
| `--version` | Tampilkan versi |

### Tool bawaan

| Tool | Permission default | Catatan |
| --- | --- | --- |
| `read_file` | allow | Baca file teks (max 256 KB) |
| `shell` | ask | PowerShell di Windows, output max 64 KB |
| `write_file` | ask | Tulis atomik via temp+rename |
| `edit_file` | ask | Ganti `old_string` unik → `new_string` |
| `grep` | allow | Spawn `rg` (ripgrep), butuh di PATH; cache sesi |
| `glob` | allow | Cari file by pola `*` / `?`; cache sesi |
| `find_symbol` | allow | Lookup definisi simbol via ctags; cache sesi |
| `web_fetch` | ask | HTTP GET, hasil ditruncate |
| `todo_write` | allow | Catat daftar todo sesi (in-memory) |

Path file divalidasi agar tidak keluar **workspace** (cwd, repo root, atau `AGNC_WORKSPACE`). Pengecualian: `read_file` boleh membaca config/sessions agnc di `~/.agnc.json` dan `~/.agnc/`.

**Pindah workspace tool:** set `AGNC_WORKSPACE=<path>` lalu restart `agnc`, atau jalankan dari repo lain. **Root MCP** terpisah — edit `mcp.servers[].args` di `~/.agnc.json`, lalu `/mcp reconnect`.

Aturan permission di `~/.agnc.json`: `always_ask`, `always_allow`, `always_deny` (subset: `shell`, `write_file`, `edit_file`, `mcp`, `web_fetch`). `always_deny` menang atas allow/ask.

Perintah shell destructive (mis. `rm -rf`, `format`) ditolak otomatis oleh safety classifier.

### MCP (stdio)

Aktifkan server di `mcp.servers[]` di config (lihat `config/agnc.example.json`). Tool diekspos ke model dengan prefix `mcp_<id>_<tool>` (mis. `mcp_workspace-fs_read_file`). Koneksi MCP dipertahankan sepanjang sesi REPL. `agnc doctor` memeriksa `mcp_config` dan `mcp_connect`.

**Dependency opsional:** tool `grep` membutuhkan [ripgrep](https://github.com/BurntSushi/ripgrep) (`rg`) di PATH. Tool `find_symbol` membutuhkan [Universal Ctags](https://github.com/universal-ctags/ctags) (`ctags`). Pasang lewat `winget install BurntSushi.ripgrep.MSVC` / `scoop install ripgrep` / `scoop install universal-ctags`, lalu cek dengan `agnc doctor`.

## Unit test

```powershell
ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure
```

## Config

File global: `%USERPROFILE%\.agnc.json` (contoh di `config/agnc.example.json`).

Config write memakai **atomic write** (`agnc_config_save_json`) agar tidak corrupt.

## Keamanan

- Folder `.keys/` hanya untuk development lokal dan **tidak boleh** di-commit ke git.
- API key tidak pernah dicetak ke log atau stdout.

Lihat `roadmap.md` untuk rencana implementasi dan `docs/smoke-test.md` untuk checklist uji manual.

## Status fase

- **Fase 0** — bootstrap, build, doctor: selesai
- **Fase 1** — `--print`, provider HTTP, read_file, shell, SSE, renderer: selesai
- **Fase 2** — write_file, edit_file, grep, glob, path safety: selesai (Windows-first)
- **Fase 3** — provider descriptor, registry gateway, model discovery: selesai
- **Fase 4** — REPL interaktif, streaming, slash commands, session persistence: selesai
- **Fase 5** — MCP stdio, multi-server, integrasi agent loop: selesai
- **Fase 6.1** — persist MCP per sesi REPL, `always_allow`, `--yes` untuk MCP: selesai
- **Fase 6.2** — line editing REPL, `web_fetch`: selesai
- **Fase 6.3** — `todo_write`: selesai
- **Fase 6.4** — modul konsol REPL Windows (input mentah, paste, permission terintegrasi): selesai
- **Fase 6.5** — env MCP spawn, `always_deny`, shell safety, CI, smoke test: selesai
- **Fase 6.6** — `/mcp`, token usage REPL, MCP auto-reconnect: selesai
- **Fase 6.7** — multi-session REPL (`/session`): selesai
- **Fase 6.8** — session SQLite (1 sesi = 1 file): selesai
- **Fase 6.9** — lazy load, append-only sync, windowed LLM context: selesai
- **Fase 6.10** — cache tool, `find_symbol` (ctags), grep truncate: selesai
- **Fase 6.11a** — agent product context (config/workspace di system prompt): selesai
- **Fase 6.12** — skills markdown ke system prompt: selesai
- **Fase 6.13** — token usage persist per sesi (`/usage`): selesai
- **Fase 6.14** — hooks shell per event agent: selesai
- **Fase 6.15** — gateway Ollama lokal + doctor + `/model` list: selesai
- **Fase 6.16** — OpenCode native, `agnc models`, cancel Ctrl+C HTTP: selesai
- **Fase 6.17+** — sub-agent, OAuth, gRPC, TUI: backlog (lihat `roadmap.md`)
