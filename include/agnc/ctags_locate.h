/*
 * ctags_locate.h
 *
 * Temukan binary ctags (Universal Ctags) di Windows/POSIX.
 */

#ifndef AGNC_CTAGS_LOCATE_H
#define AGNC_CTAGS_LOCATE_H

/* Return path ctags yang di-cache; NULL jika tidak ditemukan. Jangan free. */
const char *agnc_ctags_locate_binary(void);

#endif
