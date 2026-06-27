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
 * Workspace = AGNC_WORKSPACE env, repo root otomatis (.git / CMakeLists+src), atau cwd.
 */
agnc_status_t agnc_tool_path_validate_workspace(const char *absolute_path);

/* Return repo/workspace root (heap-owned). */
agnc_status_t agnc_tool_path_workspace_root(char **root);

/* Resolve path pencarian grep/glob; default "." -> src jika ada, else repo root. */
agnc_status_t agnc_tool_path_resolve_search(const char *path, char **resolved);

/*
 * Return 1 jika path absolut boleh dibaca read_file walau di luar tool workspace
 * (file di ~/.agnc/ atau ~/.agnc.json untuk diagnosa agnc).
 */
int agnc_tool_path_is_operator_read(const char *absolute_path);

#endif
