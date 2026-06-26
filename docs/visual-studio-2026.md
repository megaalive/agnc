# Visual Studio 2026 / CMake Open Folder

Panduan singkat untuk membuka, build, dan debug `agnc` di Visual Studio 2026.

## Prasyarat

- Visual Studio 2026 dengan workload **Desktop development with C++**
- Komponen **C++ CMake tools for Windows**
- Komponen **C++ Clang tools for Windows** (opsional)
- **Ninja** (biasanya sudah dibawa Visual Studio)

## Buka Proyek

1. Buka Visual Studio 2026.
2. Pilih **Open a local folder**.
3. Pilih folder root proyek `agnc` (yang berisi `CMakeLists.txt`).
4. Visual Studio akan mendeteksi `CMakePresets.json` dan preset **x64 Debug**.

## Build

- Pilih preset **x64 Debug** di toolbar CMake.
- Menu **Build > Build All** atau shortcut **Ctrl+Shift+B**.
- Output binary: `out/build/x64-Debug/agnc.exe`

Preset lain yang tersedia:

| Preset | Output |
| --- | --- |
| x64 Debug | `out/build/x64-Debug/agnc.exe` |
| x64 Release | `out/build/x64-Release/agnc.exe` |

## Debug

File `launch.vs.json` sudah disiapkan dengan beberapa profil debug:

| Profil | Perilaku |
| --- | --- |
| agnc (default help) | Menjalankan tanpa argumen |
| agnc --version | Cek versi |
| agnc doctor | Cek environment |
| agnc --help | Tampilkan help |

Langkah debug:

1. Pastikan preset **x64 Debug** aktif.
2. Pilih profil debug di dropdown sebelah tombol **Start** (contoh: `agnc doctor`).
3. Tekan **F5** untuk mulai debug, atau **Ctrl+F5** untuk run tanpa debugger.
4. Set breakpoint di file `.c` seperti `src/main.c` atau `src/cli/doctor.c`.

Working directory debugger diatur ke root workspace (`${workspaceRoot}`) agar path relatif seperti `.keys/` dan `config/` dapat diakses saat development.

## Troubleshooting

### CMake belum terkonfigurasi

- Menu **Project > Configure agnc** atau klik **Configure** di Solution Explorer CMake Targets.
- Pastikan preset **x64 Debug** dipilih.

### Breakpoint tidak kena

- Pastikan build aktif adalah **Debug**, bukan Release.
- Rebuild setelah mengubah source: **Build > Rebuild All**.

### IntelliSense tidak akurat

- Tunggu CMake configure selesai.
- File `compile_commands.json` dihasilkan otomatis untuk build MSVC + Ninja.

### Generator berbeda dari command line

Visual Studio memakai preset `x64-Debug` dengan generator **Ninja** dan compiler **MSVC**. Ini disengaja agar build cepat dan debugging stabil di Windows.

### Build dari terminal

Gunakan script yang sama dengan output Visual Studio:

```powershell
.\scripts\build.ps1
```

Output: `out/build/x64-Debug/agnc.exe`

Script ini memuat **VsDevShell** dan **vcpkg manifest** (`curl`, `yyjson`) otomatis.

Saat pertama kali configure, vcpkg akan mengunduh dependency — tunggu sampai selesai.

## File Terkait

| File | Fungsi |
| --- | --- |
| `CMakeLists.txt` | Definisi target `agnc` |
| `CMakePresets.json` | Preset build untuk VS 2026 |
| `launch.vs.json` | Profil debug F5 |
| `cmake/MsvcCompat.cmake` | Flag MSVC (`/utf-8`, dll.) |
| `cmake/CompilerWarnings.cmake` | Warning level |

## Alternatif Tanpa Membuka IDE

```powershell
.\scripts\build.ps1
```

Output tetap `out/build/x64-Debug/agnc.exe`, sama dengan build di Visual Studio.
