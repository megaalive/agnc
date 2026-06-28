/*
 * path.c
 *
 * Utilitas path cross-platform untuk agnc.
 * Menangani ekspansi ~ ke home directory, join path, dan pengecekan keberadaan file.
 * Semua string output dialokasikan di heap; pemanggil wajib memanggil free().
 */

#include "agnc/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <errno.h>
#define AGNC_PATH_SEP '\\'
#define agnc_mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#define AGNC_PATH_SEP '/'
#define agnc_mkdir(path, mode) mkdir(path, mode)
#endif

/*
 * Menggabungkan dua segmen path dengan separator platform yang benar.
 * Contoh: ("C:\\Users\\foo", ".agnc.json") -> "C:\\Users\\foo\\.agnc.json"
 */
static agnc_status_t agnc_path_join(const char *left, const char *right, char **output)
{
    size_t left_len;
    size_t right_len;
    char *joined;
    int needs_sep;

    if (left == NULL || right == NULL || output == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    left_len = strlen(left);
    right_len = strlen(right);

    /* Tambahkan separator hanya jika path kiri belum diakhiri slash/backslash. */
    needs_sep = left_len > 0 && left[left_len - 1] != '/' && left[left_len - 1] != '\\';

    joined = (char *)malloc(left_len + right_len + (needs_sep ? 2 : 1));
    if (joined == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    if (needs_sep) {
        snprintf(joined, left_len + right_len + 2, "%s%c%s", left, AGNC_PATH_SEP, right);
    } else {
        snprintf(joined, left_len + right_len + 1, "%s%s", left, right);
    }

    *output = joined;
    return AGNC_STATUS_OK;
}

/*
 * Mengekspansi prefix ~ menjadi home directory pengguna.
 * Jika input bukan path ber-prefix ~, hasilnya adalah salinan string input.
 */
agnc_status_t agnc_path_expand_user(const char *input, char **output)
{
    const char *home;
    const char *suffix;

    if (input == NULL || output == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    /* Path absolut/tanpa ~ tidak perlu diekspansi; cukup duplikasi string. */
    if (input[0] != '~') {
#ifdef _MSC_VER
        /* MSVC tidak menyediakan strdup(); gunakan _strdup(). */
        *output = _strdup(input);
#else
        *output = strdup(input);
#endif
        return (*output == NULL) ? AGNC_STATUS_OUT_OF_MEMORY : AGNC_STATUS_OK;
    }

    /*
     * Hanya dukung ~ dan ~/.xxx atau ~\\xxx.
     * ~username (tilde other user) sengaja tidak didukung di Fase 0.
     */
    if (input[1] != '\0' && input[1] != '/' && input[1] != '\\') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

#ifdef _WIN32
    /* Windows memakai USERPROFILE, bukan HOME. */
    home = getenv("USERPROFILE");
#else
    home = getenv("HOME");
#endif

    if (home == NULL || home[0] == '\0') {
        return AGNC_STATUS_IO_ERROR;
    }

    /* Lewati karakter ~ dan separator setelahnya sebelum join. */
    suffix = input + 1;
    if (*suffix == '/' || *suffix == '\\') {
        suffix++;
    }

    return agnc_path_join(home, suffix, output);
}

/*
 * Mengembalikan path default file config global: ~/.agnc.json
 * Hasilnya string heap-owned yang harus dibebaskan pemanggil.
 */
agnc_status_t agnc_path_default_config(char **output)
{
    return agnc_path_expand_user("~/.agnc.json", output);
}

/*
 * Mengecek apakah path ada di filesystem.
 * Return 1 jika ada, 0 jika tidak ada atau argumen NULL.
 */
int agnc_path_exists(const char *path)
{
    if (path == NULL) {
        return 0;
    }

#ifdef _WIN32
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

agnc_status_t agnc_path_ensure_dir(const char *path)
{
    char *copy = NULL;
    char *cursor = NULL;
    size_t len = 0;

    if (path == NULL || path[0] == '\0') {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

#ifdef _MSC_VER
    copy = _strdup(path);
#else
    copy = strdup(path);
#endif
    if (copy == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    len = strlen(copy);
    while (len > 0 && (copy[len - 1] == '/' || copy[len - 1] == '\\')) {
        copy[--len] = '\0';
    }

    for (cursor = copy; *cursor != '\0'; cursor++) {
        if (*cursor != '/' && *cursor != '\\') {
            continue;
        }

#ifdef _WIN32
        if ((cursor - copy) == 2 && copy[1] == ':') {
            continue;
        }
#endif

        *cursor = '\0';
        if (copy[0] != '\0' && !agnc_path_exists(copy)) {
            if (agnc_mkdir(copy, 0755) != 0 && errno != EEXIST) {
                free(copy);
                return AGNC_STATUS_IO_ERROR;
            }
        }
        *cursor = AGNC_PATH_SEP;
    }

    if (!agnc_path_exists(copy)) {
        if (agnc_mkdir(copy, 0755) != 0 && errno != EEXIST) {
            free(copy);
            return AGNC_STATUS_IO_ERROR;
        }
    }

    free(copy);
    return AGNC_STATUS_OK;
}
