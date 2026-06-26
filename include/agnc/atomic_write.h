/*
 * atomic_write.h
 *
 * Tulis file atomik: data ke file sementara lalu rename menggantikan target.
 */

#ifndef AGNC_ATOMIC_WRITE_H
#define AGNC_ATOMIC_WRITE_H

#include "agnc/status.h"

#include <stddef.h>

agnc_status_t agnc_atomic_write_file(const char *path, const void *data, size_t length);

#endif
