# agnc

CLI coding-agent pengganti OpenClaude, ditulis dalam C.

## Visual Studio 2026 (Disarankan untuk Debug)

1. Buka folder proyek ini di Visual Studio 2026 (**Open a local folder**).
2. Pilih preset **x64 Debug**.
3. Build dengan **Ctrl+Shift+B**.
4. Pilih profil debug di `launch.vs.json` (mis. `agnc doctor`) lalu tekan **F5**.

Binary: `out/build/x64-Debug/agnc.exe`

Panduan lengkap: [docs/visual-studio-2026.md](docs/visual-studio-2026.md)

## Build (Windows)

Satu lokasi output untuk semua workflow: **`out/build/x64-Debug/agnc.exe`** (sama dengan Visual Studio).

### Opsi A: Script build (disarankan)

```powershell
.\scripts\build.ps1
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

### Mode headless `--print`

```powershell
# Chat tanpa tool
.\out\build\x64-Debug\agnc.exe --print --no-tools "Say hello."

# Agent dengan tool (read, write, edit, grep, glob, shell)
.\out\build\x64-Debug\agnc.exe --print "Read README.md and summarize it."

# Shell non-interaktif (skip prompt permission)
.\out\build\x64-Debug\agnc.exe --print --yes "gunakan shell: dir"
```

| Flag | Keterangan |
| --- | --- |
| `--print "prompt"` | Query headless ke provider (OpenRouter) |
| `--no-tools` | Chat tanpa tool schema |
| `--yes` / `-y` | Setujui shell dan tulis/edit file otomatis |
| `doctor` | Cek config, libcurl, yyjson, ripgrep |
| `--version` | Tampilkan versi |

### Tool yang tersedia (Fase 1–2)

| Tool | Permission default | Catatan |
| --- | --- | --- |
| `read_file` | allow | Baca file teks (max 256 KB) |
| `shell` | ask | PowerShell di Windows, output max 64 KB |
| `write_file` | ask | Tulis atomik via temp+rename |
| `edit_file` | ask | Ganti `old_string` unik → `new_string` |
| `grep` | allow | Spawn `rg` (ripgrep), butuh di PATH |
| `glob` | allow | Cari file by pola `*` / `?` |

Path file divalidasi agar tidak keluar **workspace** (cwd atau `AGNC_WORKSPACE`).

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

Lihat `roadmap.md` untuk rencana implementasi lengkap.

## Status fase

- **Fase 0** — bootstrap, build, doctor: selesai
- **Fase 1** — `--print`, OpenRouter, read_file, shell, SSE, renderer: selesai
- **Fase 2** — write_file, edit_file, grep, glob, path safety: selesai (Windows-first)
- **Fase 3+** — provider descriptor, REPL interaktif, MCP: rencana
