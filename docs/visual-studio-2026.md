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

### CMake belum terkonfigurasi / yyjson tidak ditemukan

Penyebab umum: Visual Studio **Open Folder** menjalankan CMake tanpa toolchain vcpkg, sehingga `find_package(yyjson)` gagal.

Perbaikan otomatis: `cmake/Vcpkg.cmake` di-include **sebelum** `project()` di `CMakeLists.txt`. Modul ini:

1. Memakai `VCPKG_ROOT` jika sudah diset di environment.
2. Mencari vcpkg bawaan Visual Studio (`<edition>/VC/vcpkg`).
3. Menyetel `CMAKE_TOOLCHAIN_FILE` dan `VCPKG_MANIFEST_MODE=ON`.

Langkah manual jika masih gagal:

- Hapus cache lama: folder `out/build/x64-Debug` lalu **Project > Delete Cache and Reconfigure**.
- Pastikan preset **x64 Debug** dipilih.
- Atau dari terminal: `.\scripts\build.ps1` lalu buka ulang folder di VS.

### Peringatan analyzer MSVC (C6262, C6001, yyjson)

| Kode | File | Penanganan |
| --- | --- | --- |
| **C6262** (stack besar) | `markdown_render.c` | Buffer tabel/fence dialokasikan di **heap** (`calloc`/`free`), bukan array stack ~100–280 KB. |
| **C6001** (memori belum di-init) | `query.c` | Pointer message di-null setelah `free` di `agnc_message_list_clear`. |
| **C6297 / C28182** | header `yyjson.h` | Ditekan via `/wd6297 /wd28182` di `cmake/MsvcCompat.cmake` (false-positive di dependency). |

### Pesan linter IntelliSense (lnt-uninitialized-local)

Aturan ES.20: variabel lokal diinisialisasi saat deklarasi (`= 0`, `= NULL`, `= {0}`). Perbaikan ada di `markdown_render.c` dan `query.c`. Ini hanya saran editor; tidak memblokir build.

### Pesan VCR003 (can be made static) di args.c

**Severity Message** — bukan error/warning compiler; aman diabaikan.

Arsitektur CLI:

- Implementasi: `agnc_cli_*_impl` di `src/cli/args.c` dengan makro `AGNC_API` (`include/agnc/export.h`).
- API pemanggil: wrapper `static inline` di `include/agnc/cli.h` (dipakai `main.c`).

IntelliSense VS kadang masih menyarankan `static` pada file `.c` meski simbol sudah diekspor — [bug VS yang dikenal](https://developercommunity.visualstudio.com/t/Information-message-VCR003-given-for-ext/10729403).

Opsi jika mengganggu:

- Refresh: tutup file → **Delete Cache and Reconfigure** → buka ulang.
- **Tools > Options > Text Editor > C/C++ > IntelliSense** → *Create declaration/definition suggestion level* = **Refactoring only** atau **None**.

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
| `CMakeLists.txt` | Target `agnc`; include `Vcpkg.cmake` sebelum `project()` |
| `CMakePresets.json` | Preset build untuk VS 2026 (`VCPKG_MANIFEST_MODE`) |
| `cmake/Vcpkg.cmake` | Auto-detect toolchain vcpkg untuk Open Folder |
| `launch.vs.json` | Profil debug F5 |
| `cmake/MsvcCompat.cmake` | Flag MSVC (`/utf-8`, suppress yyjson) |
| `cmake/CompilerWarnings.cmake` | Warning level |
| `include/agnc/export.h` | Makro `AGNC_API` untuk simbol CLI |
| `include/agnc/cli.h` | Wrapper inline API CLI |
| `src/cli/console.c` | UTF-8 konsol, ANSI VT, chat REPL, spinner, permission prompt |
| `src/cli/line_edit.c` | Line editing REPL (Windows: input mentah + history; Unix: fgets) |
| `src/cli/repl.c` | Loop REPL interaktif |

## Alternatif Tanpa Membuka IDE

```powershell
.\scripts\build.ps1
```

Output tetap `out/build/x64-Debug/agnc.exe`, sama dengan build di Visual Studio.
