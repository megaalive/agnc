# Smoke Test Manual — agnc

Checklist singkat sebelum rilis atau setelah perubahan besar. Jalankan di Windows dengan binary `out/build/x64-Debug/agnc.exe` dan config valid di `%USERPROFILE%\.agnc.json`.

## Prasyarat

- [ ] Build sukses: `.\scripts\build.ps1`
- [ ] Unit test sukses: `ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure`
- [ ] API key provider terset (env atau `.keys/`)

## CLI dasar

- [ ] `agnc --version` — cetak versi tanpa error
- [ ] `agnc doctor` — config, curl, yyjson, rg (jika ada), MCP status OK atau skipped jelas

## Mode headless `--print`

- [ ] `agnc --print --no-tools "Say hello in one sentence."` — jawaban teks utuh
- [ ] `agnc --print "Read README.md and summarize in one sentence."` — tool `read_file` dipanggil
- [ ] `agnc --print --yes "List files in current directory via shell: dir"` — shell jalan tanpa prompt manual

## REPL interaktif

- [ ] `agnc` — prompt muncul, slash `/help` menampilkan bantuan
- [ ] Kirim pesan singkat — streaming/spinner, jawaban model tampil
- [ ] Ctrl+C saat request — request dibatalkan, REPL tetap hidup
- [ ] `/clear` — riwayat dihapus
- [ ] `/doctor` — status environment tampil
- [ ] Setelah turn sukses — baris `token: total N` muncul jika provider mengirim usage

## Permission

- [ ] Shell tanpa `--yes` — prompt `[y/N]` muncul di REPL
- [ ] `always_deny: ["shell"]` di config — shell ditolak tanpa prompt
- [ ] Perintah berbahaya (`rm -rf /`) — ditolak dengan pesan safety policy

## MCP (jika `mcp.servers[]` aktif)

- [ ] `/mcp` — status server MCP; `/mcp reconnect` memuat ulang koneksi
- [ ] `agnc doctor` — `mcp_connect` OK untuk server enabled
- [ ] REPL atau `--print --yes` — tool `mcp_*` dapat dipanggil model
- [ ] Server dengan `env` custom — proses child menerima variabel (cek via server MCP atau mock)

## Session

- [ ] Tutup REPL setelah chat — `%USERPROFILE%\.agnc\sessions\<nama>.json` berisi riwayat (default `current.json`)
- [ ] Buka REPL lagi — riwayat sesi aktif dimuat (`active.txt`)
- [ ] `/session` — daftar sesi; `/session new test` lalu `/session current` — pindah sesi tanpa kehilangan riwayat

## Catatan

- Test provider live (OpenRouter/Gemini) bersifat opsional; set `AGNC_TEST_LIVE_OPENROUTER=1` hanya saat regression eksplisit.
- Jangan commit secret ke log atau fixture.
