/*
 * atomic_write.c
 *
 * Tulis file dengan pola write-temp-rename agar config tidak corrupt
 * jika proses mati di tengah jalan.
 */

#include "agnc/atomic_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static char *agnc_atomic_temp_path(const char *path)
{
    size_t path_len;
    size_t suffix_len;
    char *temp_path;
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    char suffix[32];

    snprintf(suffix, sizeof(suffix), ".tmp.%lu", (unsigned long)pid);
    suffix_len = strlen(suffix);
#else
    const char *suffix = ".tmp";
    suffix_len = strlen(suffix);
#endif

    if (path == NULL) {
        return NULL;
    }

    path_len = strlen(path);
    temp_path = (char *)malloc(path_len + suffix_len + 1);
    if (temp_path == NULL) {
        return NULL;
    }

    memcpy(temp_path, path, path_len);
    memcpy(temp_path + path_len, suffix, suffix_len + 1);
    return temp_path;
}

agnc_status_t agnc_atomic_write_file(const char *path, const void *data, size_t length)
{
    char *temp_path;
    FILE *file;
    size_t written;

    if (path == NULL || path[0] == '\0' || data == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    temp_path = agnc_atomic_temp_path(path);
    if (temp_path == NULL) {
        return AGNC_STATUS_OUT_OF_MEMORY;
    }

    file = fopen(temp_path, "wb");
    if (file == NULL) {
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }

    written = fwrite(data, 1, length, file);
    if (written != length) {
        fclose(file);
        remove(temp_path);
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }

    if (fflush(file) != 0) {
        fclose(file);
        remove(temp_path);
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }

#ifdef _WIN32
    {
        int fd = _fileno(file);
        if (fd >= 0) {
            _commit(fd);
        }
    }
#else
    {
        int fd = fileno(file);
        if (fd >= 0) {
            fsync(fd);
        }
    }
#endif

    if (fclose(file) != 0) {
        remove(temp_path);
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }

#ifdef _WIN32
    if (MoveFileExA(temp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        remove(temp_path);
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }
#else
    if (rename(temp_path, path) != 0) {
        remove(temp_path);
        free(temp_path);
        return AGNC_STATUS_IO_ERROR;
    }
#endif

    free(temp_path);
    return AGNC_STATUS_OK;
}
