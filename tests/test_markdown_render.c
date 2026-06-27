/*
 * test_markdown_render.c
 *
 * Smoke test renderer markdown (tabel ASCII) — diadaptasi dari agency.
 */

#include "agnc/console.h"
#include "agnc/markdown_render.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define AGNC_MD_TEST_RESTORE_STDOUT() freopen("CONOUT$", "w", stdout)
#else
#define AGNC_MD_TEST_RESTORE_STDOUT() freopen("/dev/tty", "w", stdout)
#endif

static int render_to_temp_file(const char *markdown, const char *path)
{
    if (freopen(path, "wb", stdout) == NULL) {
        return -1;
    }

    agnc_markdown_render_body(markdown, NULL, 0);
    fflush(stdout);
    AGNC_MD_TEST_RESTORE_STDOUT();
    return 0;
}

static int temp_file_contains(const char *path, const char *needle)
{
    char buf[8192];
    size_t n;
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    const char *table_sample =
        "| **Nama** | **Tipe** |\n"
        "|---|---|\n"
        "| `.cmake` | Folder |\n"
        "| `agnc.exe` | Executable |\n"
        "| `vcpkg_installed` | Folder |\n";

    const char *bold_headers =
        "Jumlahnya 4 folder dan 38 file:\n"
        "\n"
        "**Folder (4):**\n"
        "1. .cmake\n"
        "2. CMakeFiles\n"
        "\n"
        "`**File (38):**`\n"
        "- `agnc.exe`, `agnc.exp`\n";

    const char *tmp_path = "test_markdown_render.out";

    agnc_console_init();

    agnc_markdown_render_body(table_sample, NULL, 0);

    if (render_to_temp_file(bold_headers, tmp_path) != 0) {
        fprintf(stderr, "failed to render bold headers to temp file\n");
        return 1;
    }

    if (temp_file_contains(tmp_path, "**")) {
        fprintf(stderr, "markdown renderer left literal ** in output\n");
        remove(tmp_path);
        return 1;
    }

    remove(tmp_path);
    return 0;
}
