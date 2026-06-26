/*
 * doctor.c
 *
 * Implementasi perintah `agnc doctor`.
 * Memeriksa kondisi lingkungan runtime: platform, config, folder kunci dev,
 * dan ketersediaan dependency yang akan dipakai di fase berikutnya.
 */

#include "agnc/cli.h"
#include "agnc/path.h"
#include "agnc/status.h"
#include "agnc/version.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/*
 * Mengembalikan nama platform compile-time.
 * String ini membantu diagnosis perbedaan perilaku Windows vs Unix.
 */
static const char *agnc_platform_name(void)
{
#if defined(AGNC_PLATFORM_WINDOWS)
    return "windows";
#elif defined(AGNC_PLATFORM_MACOS)
    return "macos";
#elif defined(AGNC_PLATFORM_LINUX)
    return "linux";
#else
    return "unknown";
#endif
}

/*
 * Mencetak satu baris hasil pemeriksaan dengan format kolom tetap
 * agar output doctor mudah dibaca di terminal.
 */
static void agnc_doctor_print_status(const char *name, const char *status, const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        printf("  %-18s %-10s %s\n", name, status, detail);
        return;
    }

    printf("  %-18s %s\n", name, status);
}

/*
 * Menjalankan seluruh rangkaian health check Fase 0.
 * Exit code selalu 0 karena "missing config" belum dianggap fatal.
 */
int agnc_cli_run_doctor(void)
{
    char *config_path = NULL;
    agnc_status_t status;

    printf("agnc doctor\n");
    printf("Platform: %s\n", agnc_platform_name());
    printf("Version:  %s\n", AGNC_VERSION_STRING);
    printf("\nChecks:\n");

    /* Periksa apakah file config default (~/.agnc.json) sudah ada. */
    status = agnc_path_default_config(&config_path);
    if (status != AGNC_STATUS_OK) {
        agnc_doctor_print_status("config_path", "error", agnc_status_to_string(status));
    } else if (agnc_path_exists(config_path)) {
        agnc_doctor_print_status("config_path", "ok", config_path);
    } else {
        /*
         * Config belum ada bukan error fatal di Fase 0.
         * Pengguna baru bisa mulai tanpa file config sampai Fase 1 siap.
         */
        agnc_doctor_print_status("config_path", "missing", config_path);
    }

    /*
     * Folder .keys/ hanya untuk development lokal.
     * Keberadaannya diingatkan agar tidak pernah ikut commit ke git.
     */
    if (agnc_path_exists(".keys")) {
        agnc_doctor_print_status("keys_dir", "present", "local only, must stay gitignored");
    } else {
        agnc_doctor_print_status("keys_dir", "missing", "optional for dev");
    }

    /* Dependency berikut akan di-link di Fase 1 (spike OpenRouter). */
    agnc_doctor_print_status("libcurl", "pending", "not linked yet (phase 1)");
    agnc_doctor_print_status("yyjson", "pending", "not linked yet (phase 1)");
    agnc_doctor_print_status("ripgrep", "pending", "optional until grep tool");

    /*
     * Cek binary ripgrep (`rg`) karena tool grep nanti akan spawn proses eksternal,
     * bukan implementasi pencarian sendiri.
     */
#ifdef _WIN32
    if (_access("rg.exe", 0) == 0 || _access("rg", 0) == 0) {
        agnc_doctor_print_status("rg_binary", "ok", "found in PATH or cwd");
    } else {
        agnc_doctor_print_status("rg_binary", "missing", "install ripgrep for grep tool");
    }
#else
    if (access("rg", X_OK) == 0) {
        agnc_doctor_print_status("rg_binary", "ok", "found in PATH or cwd");
    } else {
        agnc_doctor_print_status("rg_binary", "missing", "install ripgrep for grep tool");
    }
#endif

    free(config_path);
    return 0;
}
