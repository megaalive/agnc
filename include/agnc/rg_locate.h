/*
 * rg_locate.h
 *
 * Temukan binary ripgrep (rg) di Windows/POSIX.
 * _popen sering tidak punya PATH lengkap; kita cari eksplisit.
 */

#ifndef AGNC_RG_LOCATE_H
#define AGNC_RG_LOCATE_H

/* Return path rg yang di-cache; NULL jika tidak ditemukan. Jangan free. */
const char *agnc_rg_locate_binary(void);

#endif
