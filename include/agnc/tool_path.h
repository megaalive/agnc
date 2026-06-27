/*
 * tool_path.h
 *
 * Resolusi dan validasi path untuk tool file (read/write/edit/glob).
 * Mencegah path traversal keluar workspace root.
 */

#ifndef AGNC_TOOL_PATH_H
#define AGNC_TOOL_PATH_H

#include "agnc/status.h"

/*
 * Resolve path relatif/absolut ke path absolut canonical.
 * Untuk path relatif, coba cwd lalu prefix parent (build dir fallback).
 * Pemanggil wajib free(*resolved).
 */
agnc_status_t agnc_tool_path_resolve(const char *path, char **resolved);

/*
 * Pastikan path absolut berada di dalam workspace root.
 * Workspace = AGNC_WORKSPACE env, atau cwd saat ini.
 */
agnc_status_t agnc_tool_path_validate_workspace(const char *absolute_path);

#endif
