# Smoke Test Manual — agnc

Checklist singkat sebelum rilis atau setelah perubahan besar. Jalankan di Windows dengan binary `out/build/x64-Debug/agnc.exe` dan config valid di `%USERPROFILE%\.agnc.json`.

## Prasyarat

- [ ] Build sukses: `.\scripts\build.ps1`
- [ ] Unit test sukses: `ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure`
- [ ] API key provider terset (env atau `.keys/`)

## CLI dasar

- [ ] `agnc --version` — cetak versi tanpa error
- [ ] `agnc doctor` — config, curl, yyjson, rg (jika ada), ctags (jika ada), MCP status OK atau skipped jelas

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

- [ ] Tutup REPL setelah chat — `%USERPROFILE%\.agnc\sessions\<nama>.sqlite` berisi riwayat (default `current.sqlite`)
- [ ] Buka REPL lagi — riwayat sesi aktif dimuat (`active.txt`)
- [ ] `/session` — daftar sesi; `/session new test` lalu `/session current` — pindah sesi tanpa kehilangan riwayat
- [ ] `/session delete test` — file sesi terhapus; jika sesi aktif, REPL pindah ke `current` kosong
- [ ] Lazy load: buka REPL dengan sesi panjang — notifikasi "memuat N pesan terakhir" (bukan full history di RAM)
- [ ] `/compact 24` — RAM + SQLite diringkas; file `.sqlite` lebih kecil setelah compact

## Code lookup (Fase 6.10)

- [ ] `agnc doctor` — `ctags` OK (Universal Ctags terpasang)
- [ ] `--print --yes "Find symbol agnc_query_run in src using find_symbol tool"` — definisi di `query.c`
- [ ] Panggil `grep` dua kali dengan argumen sama — hasil identik (cache sesi; cek via verbose log tidak re-spawn rg)

## Agent product context (Fase 6.11a)

- [ ] `agnc doctor` — baris `tool_workspace` menampilkan root aktif
- [ ] `/help` — bagian "Workspace dan config agnc" tampil
- [ ] `--print --yes "Where is agnc global config and how do I change workspace?"` — jawaban merujuk `~/.agnc.json` dan `AGNC_WORKSPACE`, bukan Claude Desktop

## Skills (Fase 6.12)

- [ ] Buat `.agnc/skills/test.md` dengan instruksi unik — model mengikutinya saat `--print`
- [ ] `/skills` di REPL — daftar file; `/skills reload` setelah edit file
- [ ] `agnc doctor` — baris `skills` menampilkan jumlah file

## Catatan

- Test provider live (OpenRouter/Gemini) bersifat opsional; set `AGNC_TEST_LIVE_OPENROUTER=1` hanya saat regression eksplisit.
- Jangan commit secret ke log atau fixture.
