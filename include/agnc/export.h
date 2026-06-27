/*
 * export.h
 *
 * Makro visibilitas simbol API agnc.
 * AGNC_BUILDING diset di target agnc agar MSVC menandai simbol sebagai diekspor
 * (membantu IntelliSense mengenali fungsi publik, bukan kandidat static).
 */

#ifndef AGNC_EXPORT_H
#define AGNC_EXPORT_H

#if defined(_WIN32) && defined(AGNC_BUILDING)
#define AGNC_API __declspec(dllexport)
#elif defined(_WIN32)
#define AGNC_API __declspec(dllimport)
#else
#define AGNC_API
#endif

#endif /* AGNC_EXPORT_H */
