/*
 * markdown_render.h
 *
 * Render body respons: tabel grid ASCII, code fence, inline markdown.
 */

#ifndef AGNC_MARKDOWN_RENDER_H
#define AGNC_MARKDOWN_RENDER_H

/* time_prefix NULL = tanpa prefix waktu; continuation_indent untuk baris lanjutan. */
void agnc_markdown_render_body(const char *text, const char *time_prefix, int continuation_indent);

#endif
