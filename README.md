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

## Perintah Awal

```powershell
.\out\build\x64-Debug\agnc.exe --version
.\out\build\x64-Debug\agnc.exe doctor
.\out\build\x64-Debug\agnc.exe --help
```

## Config

Salin contoh config ke home directory:

```powershell
copy config\agnc.example.json $env:USERPROFILE\.agnc.json
```

## Keamanan

Folder `.keys/` hanya untuk development lokal dan **tidak boleh** di-commit ke git.

Lihat `roadmap.md` untuk rencana implementasi lengkap.
