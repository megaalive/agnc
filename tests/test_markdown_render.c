/*
 * test_markdown_render.c
 *
 * Smoke test renderer markdown (tabel ASCII) — diadaptasi dari agency.
 */

#include "agnc/console.h"
#include "agnc/markdown_render.h"

#include <stdio.h>

int main(void)
{
    const char *sample =
        "| **Nama** | **Tipe** |\n"
        "|---|---|\n"
        "| `.cmake` | Folder |\n"
        "| `agnc.exe` | Executable |\n"
        "| `vcpkg_installed` | Folder |\n";

    agnc_console_init();
    agnc_markdown_render_body(sample, NULL, 0);
    return 0;
}
