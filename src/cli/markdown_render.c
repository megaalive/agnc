/*
 * markdown_render.c
 *
 * Render respons assistant: tabel ASCII, code fence, inline markdown.
 * Diadaptasi dari proyek agency; mode perbandingan "X vs Y" dinonaktifkan untuk agnc.
 */

#include "agnc/markdown_render.h"

#include "agnc/console.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define ANSI_RESET AGNC_ANSI_RESET
#define ANSI_DIM AGNC_ANSI_DIM
#define ANSI_BOLD AGNC_ANSI_BOLD
#define ANSI_ITALIC "\033[3m"
#define ANSI_CODE AGNC_ANSI_CODE

#define MD_MAX_COLS 6
#define MD_MAX_ROWS 48
#define MD_CELL_MAX 384
#define MD_LINE_MAX 1024

/* Buffer baris/tabel besar dialokasikan di heap (bukan stack) agar lolos MSVC C6262. */
typedef char agnc_md_line_buf_t[MD_LINE_MAX];
typedef char agnc_md_table_row_t[MD_MAX_COLS][MD_CELL_MAX];
typedef char agnc_md_wrapped_col_t[16][MD_CELL_MAX];

#define AGNC_DIR_TREE_FENCE "```tree"

#define VT() agnc_console_vt_enabled()

static int agnc_terminal_width(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(out, &info)) {
        int w = (int)info.srWindow.Right - (int)info.srWindow.Left + 1;
        if (w >= 40) return w;
    }
#endif
    return 100;
}

static void trim_spaces(char* s) {
    size_t len = 0;
    char* start = NULL;
    if (!s) return;
    start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static int is_table_separator_line(const char* line) {
    const char* p = line;
    int dash = 0;
    if (!line || line[0] != '|') return 0;
    for (p = line; *p; p++) {
        if (*p == '-') dash = 1;
        else if (*p != '|' && *p != ':' && *p != ' ' && *p != '\t') return 0;
    }
    return dash;
}

static int is_table_row_line(const char* line) {
    if (!line || line[0] != '|' || !strchr(line + 1, '|')) return 0;
    return !is_table_separator_line(line);
}

static int is_fence_marker(const char* line) {
    if (!line) return 0;
    if (strncmp(line, "```", 3) == 0) return 1;
    return 0;
}

static int is_tree_fence_open(const char* line) {
    if (!line) return 0;
    return strncmp(line, AGNC_DIR_TREE_FENCE, strlen(AGNC_DIR_TREE_FENCE)) == 0;
}

static int is_box_drawing_line(const char* line) {
    const char* p = line;
    int special = 0;
    int plain = 0;
    if (!line || !line[0]) return 0;

    /* Markdown bold — bukan diagram ASCII (mis. "**Folder (4):**"). */
    if (strstr(line, "**") != NULL) return 0;

    /* Diagram butuh garis struktural (+, |, ---), bukan hanya tanda baca. */
    if (strchr(line, '+') == NULL && strchr(line, '|') == NULL && strstr(line, "---") == NULL) {
        return 0;
    }

    for (p = line; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80U) {
            special++;
            continue;
        }
        if (strchr("+-|/\\_:=*#<>[]() .", c)) special++;
        else if (isalnum(c)) plain++;
        else special++;
    }
    return special > plain;
}

static void write_indent(int indent) {
    int i = 0;
    for (i = 0; i < indent; i++) putchar(' ');
}

static void write_plain(const char* text, size_t len) {
    if (!text || len == 0) return;
    fwrite(text, 1, len, stdout);
}

/* Model kadang kirim `` `**Heading:**` `` — treat sebagai bold, bukan inline code. */
static int agnc_md_is_wrapped_bold(const char* inner, size_t inner_len) {
    char buf[MD_CELL_MAX];
    size_t len;

    if (inner == NULL || inner_len < 4) {
        return 0;
    }

    if (inner_len >= sizeof(buf)) {
        inner_len = sizeof(buf) - 1;
    }
    memcpy(buf, inner, inner_len);
    buf[inner_len] = '\0';
    trim_spaces(buf);
    len = strlen(buf);
    if (len < 4) {
        return 0;
    }
    return buf[0] == '*' && buf[1] == '*' && buf[len - 2] == '*' && buf[len - 1] == '*';
}

static void agnc_md_write_wrapped_bold(const char* inner, size_t inner_len, int use_vt) {
    char buf[MD_CELL_MAX];
    size_t len;

    if (inner_len >= sizeof(buf)) {
        inner_len = sizeof(buf) - 1;
    }
    memcpy(buf, inner, inner_len);
    buf[inner_len] = '\0';
    trim_spaces(buf);
    len = strlen(buf);

    if (use_vt) {
        fputs(ANSI_BOLD, stdout);
    }
    write_plain(buf + 2, len - 4);
    if (use_vt) {
        fputs(ANSI_RESET, stdout);
    }
}

static int markdown_plain_width(const char* text) {
    char buf[MD_CELL_MAX] = {0};
    size_t bi = 0;
    size_t i = 0;
    size_t len = 0;

    if (!text) return 0;
    len = strlen(text);
    while (i < len && bi < sizeof(buf) - 1) {
        if (i + 1 < len && text[i] == '*' && text[i + 1] == '*') {
            i += 2;
            while (i < len && !(text[i] == '*' && i + 1 < len && text[i + 1] == '*')) {
                buf[bi++] = text[i++];
            }
            if (i + 1 < len) i += 2;
            continue;
        }
        if (text[i] == '`') {
            i++;
            while (i < len && text[i] != '`') buf[bi++] = text[i++];
            if (i < len) i++;
            continue;
        }
        if (text[i] == '*' && (i + 1 >= len || text[i + 1] != '*')) {
            i++;
            while (i < len && text[i] != '*') buf[bi++] = text[i++];
            if (i < len) i++;
            continue;
        }
        if (text[i] != '\n') buf[bi++] = text[i];
        i++;
    }
    buf[bi] = '\0';
    return (int)strlen(buf);
}

static void write_inline_markdown_plain(const char* text, size_t text_len) {
    size_t i = 0;
    if (!text) return;
    if (text_len == 0) text_len = strlen(text);

    while (i < text_len) {
        if (i + 1 < text_len && text[i] == '*' && text[i + 1] == '*') {
            const char* end = strstr(text + i + 2, "**");
            if (end && (size_t)(end - text) <= text_len) {
                write_plain(text + i + 2, (size_t)(end - (text + i + 2)));
                i = (size_t)(end - text) + 2;
                continue;
            }
        }
        if (text[i] == '*' && (i + 1 >= text_len || text[i + 1] != '*')) {
            const char* end = strchr(text + i + 1, '*');
            if (end && (size_t)(end - text) <= text_len && (end == text + i + 1 || end[-1] != '*')) {
                write_plain(text + i + 1, (size_t)(end - (text + i + 1)));
                i = (size_t)(end - text) + 1;
                continue;
            }
        }
        if (text[i] == '`') {
            const char* end = strchr(text + i + 1, '`');
            if (end && (size_t)(end - text) <= text_len) {
                size_t inner_len = (size_t)(end - (text + i + 1));
                if (agnc_md_is_wrapped_bold(text + i + 1, inner_len)) {
                    agnc_md_write_wrapped_bold(text + i + 1, inner_len, 0);
                    i = (size_t)(end - text) + 1;
                    continue;
                }
                write_plain(text + i + 1, inner_len);
                i = (size_t)(end - text) + 1;
                continue;
            }
        }
        fputc(text[i], stdout);
        i++;
    }
}

static void write_inline_markdown(const char* text, size_t text_len) {
    size_t i = 0;

    if (!text) return;
    if (text_len == 0) text_len = strlen(text);
    if (!VT()) {
        write_inline_markdown_plain(text, text_len);
        return;
    }

    while (i < text_len) {
        if (i + 1 < text_len && text[i] == '*' && text[i + 1] == '*') {
            const char* end = strstr(text + i + 2, "**");
            if (end && (size_t)(end - text) <= text_len) {
                fputs(ANSI_BOLD, stdout);
                write_plain(text + i + 2, (size_t)(end - (text + i + 2)));
                fputs(ANSI_RESET, stdout);
                i = (size_t)(end - text) + 2;
                continue;
            }
        }
        if (text[i] == '*' && (i + 1 >= text_len || text[i + 1] != '*')) {
            const char* end = strchr(text + i + 1, '*');
            if (end && (size_t)(end - text) <= text_len && (end == text + i + 1 || end[-1] != '*')) {
                fputs(ANSI_ITALIC, stdout);
                write_plain(text + i + 1, (size_t)(end - (text + i + 1)));
                fputs(ANSI_RESET, stdout);
                i = (size_t)(end - text) + 1;
                continue;
            }
        }
        if (text[i] == '`') {
            const char* end = strchr(text + i + 1, '`');
            if (end && (size_t)(end - text) <= text_len) {
                size_t inner_len = (size_t)(end - (text + i + 1));
                if (agnc_md_is_wrapped_bold(text + i + 1, inner_len)) {
                    agnc_md_write_wrapped_bold(text + i + 1, inner_len, 1);
                    i = (size_t)(end - text) + 1;
                    continue;
                }
                fputs(ANSI_CODE, stdout);
                write_plain(text + i + 1, inner_len);
                fputs(ANSI_RESET, stdout);
                i = (size_t)(end - text) + 1;
                continue;
            }
        }
        fputc(text[i], stdout);
        i++;
    }
    fputs(ANSI_RESET, stdout);
}

static void normalize_cell_html(char* cell) {
    char* dst;
    const char* src = NULL;
    if (!cell) return;
    dst = cell;
    src = cell;
    while (*src) {
        if (strncmp(src, "<br>", 4) == 0 || strncmp(src, "<br/>", 5) == 0 ||
            strncmp(src, "<br />", 6) == 0) {
            *dst++ = '\n';
            if (strncmp(src, "<br />", 6) == 0) src += 6;
            else if (src[3] == '/') src += 5;
            else src += 4;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static int cell_display_width(const char* text) {
    return markdown_plain_width(text);
}

static int split_table_row(const char* line, char cells[MD_MAX_COLS][MD_CELL_MAX], int* out_cols) {
    char buf[MD_LINE_MAX];
    char* token;
    int col = 0;

    if (!line || !out_cols) return 0;
    *out_cols = 0;
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    token = strtok(buf, "|");
    while (token) {
        if (col >= MD_MAX_COLS) break;
        strncpy(cells[col], token, MD_CELL_MAX - 1);
        cells[col][MD_CELL_MAX - 1] = '\0';
        trim_spaces(cells[col]);
        normalize_cell_html(cells[col]);
        col++;
        token = strtok(NULL, "|");
    }
    if (col > 0 && cells[0][0] == '\0') {
        memmove(cells[0], cells[1], sizeof(cells[0]));
        for (int i = 1; i < col - 1; i++) memcpy(cells[i], cells[i + 1], sizeof(cells[i]));
        col--;
    }
    if (col > 0 && cells[col - 1][0] == '\0') col--;
    *out_cols = col;
    return col > 0;
}

static int is_wrap_break_char(char ch) {
    return ch == ' ' || ch == '-' || ch == '/' || ch == ',' || ch == ';' || ch == '(' || ch == ')';
}

static int wrap_cell_lines(const char* cell, int width, char lines[16][MD_CELL_MAX], int max_lines) {
    const char* segment = NULL;
    int count = 0;
    char segment_buf[MD_CELL_MAX];

    if (!cell || width < 4) width = 4;

    segment = cell;
    while (*segment && count < max_lines) {
        const char* next_nl = strchr(segment, '\n');
        size_t seg_len = next_nl ? (size_t)(next_nl - segment) : strlen(segment);

        if (seg_len >= sizeof(segment_buf)) seg_len = sizeof(segment_buf) - 1;
        memcpy(segment_buf, segment, seg_len);
        segment_buf[seg_len] = '\0';
        trim_spaces(segment_buf);

        {
            const char* p = segment_buf;
            while (*p && count < max_lines) {
                size_t len = strlen(p);
                size_t take = len;
                const char* break_at = NULL;
                if (take > (size_t)width) {
                    take = (size_t)width;
                    break_at = p + take;
                    while (break_at > p && !is_wrap_break_char(*break_at)) break_at--;
                    if (break_at > p) {
                        take = (size_t)(break_at - p);
                        if (take > 0 && is_wrap_break_char(p[take - 1])) take--;
                    } else {
                        take = (size_t)width;
                    }
                    {
                        const char* open = strchr(p, '(');
                        if (open && open < p + take && take < len) {
                            const char* close = strchr(open, ')');
                            if (close) {
                                size_t prefix_len = (size_t)(open - p);
                                size_t paren_len = (size_t)(close - open + 1);
                                if (prefix_len > 0 && prefix_len + paren_len <= (size_t)width) {
                                    take = prefix_len + paren_len;
                                } else if (prefix_len > 0 && prefix_len <= (size_t)width) {
                                    take = prefix_len;
                                    while (take > 0 && p[take - 1] == ' ') take--;
                                }
                            } else if (open > p) {
                                take = (size_t)(open - p);
                                while (take > 0 && p[take - 1] == ' ') take--;
                            }
                        }
                    }
                    {
                        size_t rest = len - take;
                        while (rest > 0 && (p[take] == ' ' || is_wrap_break_char(p[take]))) {
                            take++;
                            rest--;
                        }
                        if (rest > 0 && rest < 4 && take > 4) {
                            break_at = p + take - 1;
                            while (break_at > p && !is_wrap_break_char(*break_at)) break_at--;
                            if (break_at > p) {
                                take = (size_t)(break_at - p);
                                if (take > 0 && is_wrap_break_char(p[take - 1])) take--;
                            }
                        }
                    }
                }
                strncpy(lines[count], p, take);
                lines[count][take] = '\0';
                trim_spaces(lines[count]);
                count++;
                p += take;
                while (*p == ' ' || is_wrap_break_char(*p)) {
                    if (*p == ' ') p++;
                    else if ((*p == '-' || *p == '/') && p[1] == ' ') p += 2;
                    else break;
                }
            }
        }

        if (!next_nl) break;
        segment = next_nl + 1;
    }

    if (count == 0) {
        lines[0][0] = '\0';
        count = 1;
    }
    return count;
}

static int cell_max_line_width(const char* cell) {
    int max_w = 0;
    const char* p = NULL;
    if (!cell) return 0;
    p = cell;
    while (*p) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > max_w) max_w = len;
        if (!nl) break;
        p = nl + 1;
    }
    return max_w;
}

static int str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int is_generic_table_header(const char* cell) {
    static const char* words[] = {
        "aspek", "fitur", "kriteria", "item", "field", "nama", "karakteristik", "poin", "topik", "kategori",
        "keterangan", "penjelasan", "deskripsi", "value", "nilai", "detail", "informasi", "atribut", "property",
        "kolom", "istilah", "unsur", "parameter", NULL};
    char buf[MD_CELL_MAX];
    int i = 0;

    if (!cell) return 0;
    strncpy(buf, cell, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_spaces(buf);
    for (i = 0; words[i]; i++) {
        if (str_ieq(buf, words[i])) return 1;
    }
    return 0;
}

static int is_two_column_comparison(char table[MD_MAX_ROWS][MD_MAX_COLS][MD_CELL_MAX], int rows) {
    if (rows < 2) return 0;
    if (table[0][0][0] == '\0' || table[0][1][0] == '\0') return 0;
    if (is_generic_table_header(table[0][0])) return 0;
    if (is_generic_table_header(table[0][1])) return 0;
    return 1;
}

static void render_vs_legend(int indent, const char* const* names, int count) {
    int j = 0;

    write_indent(indent);
    for (j = 0; j < count; j++) {
        if (j > 0) {
            if (VT()) fputs(ANSI_DIM, stdout);
            fputs("  vs  ", stdout);
            if (VT()) fputs(ANSI_RESET, stdout);
        }
        write_inline_markdown(names[j], strlen(names[j]));
    }
    putchar('\n');
}

static void render_row_separator(int indent) {
    write_indent(indent);
    if (VT()) fputs(ANSI_DIM, stdout);
    fputs("---\n", stdout);
    if (VT()) fputs(ANSI_RESET, stdout);
}

static int measure_label_width(const char* const* labels, int count) {
    int label_w = 0;
    int j = 0;

    for (j = 0; j < count; j++) {
        int hl = markdown_plain_width(labels[j]);
        if (hl > label_w) label_w = hl;
    }
    if (label_w > 20) label_w = 20;
    if (label_w < 6) label_w = 6;
    return label_w;
}

static void render_label_value_pairs(int indent, const char* const* labels, int count, int label_w, int value_w,
    const char* const* values) {
    int j = 0;

    for (j = 0; j < count; j++) {
        char wrapped[16][MD_CELL_MAX];
        int line_count = wrap_cell_lines(values[j], value_w, wrapped, 16);
        int r = 0;

        for (r = 0; r < line_count; r++) {
            if (r == 0) {
                int hl = markdown_plain_width(labels[j]);
                int pad = 0;
                write_indent(indent);
                write_inline_markdown(labels[j], strlen(labels[j]));
                pad = label_w - hl;
                while (pad-- > 0) putchar(' ');
                fputs(" | ", stdout);
            } else {
                write_indent(indent + label_w + 3);
            }
            write_inline_markdown(wrapped[r], strlen(wrapped[r]));
            putchar('\n');
        }
    }
}

static int table_border_width(const int* col_width, int cols) {
    int total = cols + 1;
    int c = 0;
    for (c = 0; c < cols; c++) total += col_width[c];
    return total;
}

static void draw_table_rule(int indent, const int* col_width, int cols) {
    int c = 0;
    int j = 0;
    if (VT()) fputs(ANSI_DIM, stdout);
    write_indent(indent);
    putchar('+');
    for (c = 0; c < cols; c++) {
        for (j = 0; j < col_width[c]; j++) putchar('-');
        putchar('+');
    }
    putchar('\n');
    if (VT()) fputs(ANSI_RESET, stdout);
}

static void fit_table_columns(int cols, int col_width[MD_MAX_COLS], int rows,
    agnc_md_table_row_t* table, int term_w) {
    int budget = term_w - (cols + 1);
    int c = 0;
    int i = 0;

    if (budget < cols * 8) budget = cols * 8;

    if (cols >= 3) {
        int label_w = 0;
        for (i = 0; i < rows; i++) {
            int len = cell_max_line_width(table[i][0]);
            if (len > label_w) label_w = len;
        }
        col_width[0] = label_w + 2;
        if (col_width[0] > 24) col_width[0] = 24;
        if (col_width[0] < 8) col_width[0] = 8;
        {
            int rest = budget - col_width[0];
            int data_cols = cols - 1;
            int per = rest / data_cols;
            if (per < 14) per = 14;
            for (c = 1; c < cols; c++) col_width[c] = per;
        }
    } else if (cols == 2) {
        int c0_need = 0;
        for (i = 0; i < rows; i++) {
            int len = cell_max_line_width(table[i][0]);
            if (len > c0_need) c0_need = len;
        }
        col_width[0] = c0_need + 2;
        if (col_width[0] > 28) col_width[0] = 28;
        if (col_width[0] < 8) col_width[0] = 8;
        col_width[1] = budget - col_width[0];
        if (col_width[1] < 16) {
            col_width[1] = 16;
            col_width[0] = budget - 16;
            if (col_width[0] < 8) col_width[0] = 8;
        }
    } else {
        int per_col = budget / cols;
        if (per_col < 12) per_col = 12;
        for (c = 0; c < cols; c++) {
            int max_len = 0;
            for (i = 0; i < rows; i++) {
                int len = cell_max_line_width(table[i][c]);
                if (len > max_len) max_len = len;
            }
            col_width[c] = max_len + 2;
            if (col_width[c] > per_col) col_width[c] = per_col;
            if (col_width[c] < 6) col_width[c] = 6;
        }
    }

    while (table_border_width(col_width, cols) > term_w && col_width[cols - 1] > 8) {
        col_width[cols - 1]--;
    }
    while (table_border_width(col_width, cols) > term_w && cols > 1 && col_width[0] > 8) {
        col_width[0]--;
    }
}

static void render_grid_table(int indent, agnc_md_table_row_t* table, int rows, int cols,
    const int* col_width) {
    /* ~110 KB per kolom jika di stack; pakai heap + inisialisasi pointer untuk linter. */
    agnc_md_wrapped_col_t* wrapped = NULL;
    int* line_counts = NULL;
    int i = 0;
    int c = 0;

    wrapped = (agnc_md_wrapped_col_t*)calloc((size_t)MD_MAX_COLS, sizeof(agnc_md_wrapped_col_t));
    line_counts = (int*)calloc((size_t)MD_MAX_COLS, sizeof(int));
    if (wrapped == NULL || line_counts == NULL) {
        free(wrapped);
        free(line_counts);
        return;
    }

    draw_table_rule(indent, col_width, cols);

    for (i = 0; i < rows; i++) {
        int row_h = 1;
        int r = 0;
        int is_header = (i == 0);

        for (c = 0; c < cols; c++) {
            line_counts[c] = wrap_cell_lines(table[i][c], col_width[c] - 2, wrapped[c], 16);
            if (line_counts[c] > row_h) row_h = line_counts[c];
        }

        for (r = 0; r < row_h; r++) {
            write_indent(indent);
            putchar('|');
            for (c = 0; c < cols; c++) {
                const char* cell_line = (r < line_counts[c]) ? wrapped[c][r] : "";
                int pad = col_width[c] - 2 - cell_display_width(cell_line);
                putchar(' ');
                if (VT() && is_header) fputs(ANSI_BOLD, stdout);
                write_inline_markdown(cell_line, strlen(cell_line));
                if (VT() && is_header) fputs(ANSI_RESET, stdout);
                if (pad > 0) {
                    int p = 0;
                    for (p = 0; p < pad; p++) putchar(' ');
                }
                putchar(' ');
                putchar('|');
            }
            putchar('\n');
        }

        if (i == 0 || i < rows - 1) draw_table_rule(indent, col_width, cols);
    }

    free(wrapped);
    free(line_counts);
}

static int extract_comparison_labels(char table[MD_MAX_ROWS][MD_MAX_COLS][MD_CELL_MAX], int rows, int cols,
    const char* labels[MD_MAX_COLS]) {
    int count = 0;
    int non_empty = 0;
    int data_has_aspect = 0;
    int j = 0;

    for (j = 0; j < cols; j++) {
        if (table[0][j][0] != '\0') non_empty++;
    }
    if (rows >= 2 && table[1][0][0] != '\0') data_has_aspect = 1;

    /* | **Indonesia** | **Iran** | lalu data 3 kolom dengan aspek di kolom 0 */
    if (cols >= 3 && data_has_aspect && table[0][0][0] != '\0' && !is_generic_table_header(table[0][0]) &&
        non_empty <= cols - 1) {
        for (j = 0; j < cols - 1; j++) {
            if (table[0][j][0] != '\0') labels[count++] = table[0][j];
        }
        return count;
    }

    if (cols >= 3 && table[0][0][0] == '\0') {
        for (j = 1; j < cols; j++) {
            if (table[0][j][0] != '\0') labels[count++] = table[0][j];
        }
        return count;
    }

    if (table[0][0][0] != '\0' && is_generic_table_header(table[0][0])) {
        for (j = 1; j < cols; j++) {
            if (table[0][j][0] != '\0') labels[count++] = table[0][j];
        }
        return count;
    }

    if (cols >= 3 && data_has_aspect) {
        for (j = 1; j < cols; j++) {
            if (table[0][j][0] != '\0') labels[count++] = table[0][j];
        }
        return count;
    }

    for (j = 0; j < cols; j++) {
        if (table[0][j][0] != '\0') labels[count++] = table[0][j];
    }
    return count;
}

/* Tabel 3+ kolom: kartu per aspek (kolom 0 = label, sisanya = nilai) */
static void render_comparison_table(int indent, char table[MD_MAX_ROWS][MD_MAX_COLS][MD_CELL_MAX], int rows, int cols,
    int term_w) {
    const char* labels[MD_MAX_COLS] = {0};
    const char* values[MD_MAX_COLS] = {0};
    int label_count = 0;
    int value_start = 0;
    int label_w = 0;
    int value_w = 0;
    int i = 0;
    int j = 0;

    label_count = extract_comparison_labels(table, rows, cols, labels);
    if (label_count <= 0) return;

    value_start = cols - label_count;
    if (cols >= 3 && rows >= 2 && table[1][0][0] != '\0') value_start = 1;
    if (value_start < 0) value_start = 0;

    label_w = measure_label_width(labels, label_count);
    value_w = term_w - indent - label_w - 3;
    if (value_w < 20) value_w = 20;

    render_vs_legend(indent, labels, label_count);

    for (i = 1; i < rows; i++) {
        if (i > 1) render_row_separator(indent);

        if (value_start > 0 && table[i][0][0] != '\0') {
            write_indent(indent);
            if (VT() && !strstr(table[i][0], "**")) fputs(ANSI_BOLD, stdout);
            write_inline_markdown(table[i][0], strlen(table[i][0]));
            if (VT() && !strstr(table[i][0], "**")) fputs(ANSI_RESET, stdout);
            putchar('\n');
        }

        for (j = 0; j < label_count; j++) values[j] = table[i][value_start + j];
        render_label_value_pairs(indent, labels, label_count, label_w, value_w, values);
    }
}

/* Tabel 2 kolom perbandingan: header = nama produk, baris = pasangan nilai */
static void render_two_column_comparison(int indent, char table[MD_MAX_ROWS][MD_MAX_COLS][MD_CELL_MAX], int rows,
    int term_w) {
    const char* labels[2] = {0};
    const char* values[2] = {0};
    int label_w = 0;
    int value_w = 0;
    int i = 0;

    labels[0] = table[0][0];
    labels[1] = table[0][1];
    label_w = measure_label_width(labels, 2);
    value_w = term_w - indent - label_w - 3;
    if (value_w < 20) value_w = 20;

    render_vs_legend(indent, labels, 2);

    for (i = 1; i < rows; i++) {
        if (i > 1) render_row_separator(indent);
        values[0] = table[i][0];
        values[1] = table[i][1];
        render_label_value_pairs(indent, labels, 2, label_w, value_w, values);
    }
}

static void render_table_block(const char* const* lines, int line_count, int indent) {
    /* Satu baris tabel ~110 KB di stack; simpan semua baris di heap. */
    agnc_md_table_row_t* table = NULL;
    int rows = 0;
    int cols = 0;
    int col_width[MD_MAX_COLS] = {0};
    int term_w = 0;
    int i = 0;

    table = (agnc_md_table_row_t*)calloc((size_t)MD_MAX_ROWS, sizeof(agnc_md_table_row_t));
    if (table == NULL) {
        return;
    }

    for (i = 0; i < line_count && rows < MD_MAX_ROWS; i++) {
        int row_cols = 0;
        if (!is_table_row_line(lines[i])) continue;
        if (!split_table_row(lines[i], table[rows], &row_cols)) continue;
        if (row_cols > cols) cols = row_cols;
        rows++;
    }
    if (rows == 0 || cols == 0) {
        free(table);
        return;
    }

    term_w = agnc_terminal_width() - indent;
    if (term_w < 40) term_w = 40;

    /*
     * agnc: selalu grid ASCII untuk tabel data (listing file, key-value, dll.).
     * Mode "X vs Y" dari agency hanya cocok untuk artikel perbandingan entitas,
     * bukan tabel direktori — memicu output membingungkan (Nama File vs Keterangan).
     */
    fit_table_columns(cols, col_width, rows, table, term_w);
    render_grid_table(indent, table, rows, cols, col_width);
    free(table);
}

static void render_prefixed_line(const char* time_prefix, int* first_line, int paragraph_continue, int continuation_indent,
    const char* line) {
    if (*first_line) {
        if (time_prefix && time_prefix[0] != '\0') printf("%s - ", time_prefix);
        *first_line = 0;
    } else if (paragraph_continue) {
        write_indent(continuation_indent);
    }
    write_inline_markdown(line, strlen(line));
    putchar('\n');
}

static void render_tree_block(const char* const* lines, int line_count, const char* time_prefix, int* first_line,
    int continuation_indent) {
    int i = 0;
    for (i = 0; i < line_count; i++) {
        if (*first_line) {
            if (time_prefix && time_prefix[0] != '\0') printf("%s - ", time_prefix);
            *first_line = 0;
        } else {
            write_indent(continuation_indent);
        }
        if (VT()) fputs(ANSI_CODE, stdout);
        fputs(lines[i], stdout);
        if (VT()) fputs(ANSI_RESET, stdout);
        putchar('\n');
    }
}

static void render_preformatted_block(const char* const* lines, int line_count, const char* time_prefix, int* first_line,
    int continuation_indent) {
    int i = 0;
    if (VT()) fputs(ANSI_CODE, stdout);
    for (i = 0; i < line_count; i++) {
        if (*first_line) {
            if (time_prefix && time_prefix[0] != '\0') printf("%s - ", time_prefix);
            *first_line = 0;
        } else {
            write_indent(continuation_indent);
        }
        fputs(lines[i], stdout);
        putchar('\n');
    }
    if (VT()) fputs(ANSI_RESET, stdout);
}

void agnc_markdown_render_body(const char* text, const char* time_prefix, int continuation_indent) {
    const char* cursor = NULL;
    int first_line = 1;
    int paragraph_continue = 0;

    if (!text) return;
    cursor = text;

    /* State machine per baris: fence, tabel, box-drawing, atau inline markdown.
     * Blok multi-baris memakai calloc (bukan array stack besar) untuk MSVC C6262. */
    while (cursor && *cursor) {
        const char* line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        char line[MD_LINE_MAX] = {0};

        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        trim_spaces(line);

        if (is_fence_marker(line)) {
            const char* block_end = line_end ? line_end + 1 : cursor + line_len;
            agnc_md_line_buf_t* block_lines = NULL;
            const char* block_ptrs[128] = {0};
            int block_count = 0;
            int use_tree_renderer = is_tree_fence_open(line);
            int bi = 0;

            block_lines = (agnc_md_line_buf_t*)calloc(128, sizeof(agnc_md_line_buf_t));
            if (block_lines == NULL) {
                cursor = line_end ? line_end + 1 : cursor + line_len;
                continue;
            }

            while (block_end && *block_end && block_count < 128) {
                const char* next_end = strchr(block_end, '\n');
                size_t blen = next_end ? (size_t)(next_end - block_end) : strlen(block_end);
                if (blen >= sizeof(block_lines[0])) blen = sizeof(block_lines[0]) - 1;
                memcpy(block_lines[block_count], block_end, blen);
                block_lines[block_count][blen] = '\0';
                if (is_fence_marker(block_lines[block_count])) break;
                block_count++;
                block_end = next_end ? next_end + 1 : block_end + blen;
            }
            for (bi = 0; bi < block_count; bi++) block_ptrs[bi] = block_lines[bi];
            if (use_tree_renderer) {
                render_tree_block(block_ptrs, block_count, time_prefix, &first_line, continuation_indent);
            } else {
                render_preformatted_block(block_ptrs, block_count, time_prefix, &first_line, continuation_indent);
            }
            free(block_lines);
            paragraph_continue = 0;
            cursor = block_end;
            while (cursor && *cursor && *cursor != '\n') cursor++;
            if (cursor && *cursor == '\n') cursor++;
            continue;
        }

        if (is_table_row_line(line) || is_table_separator_line(line)) {
            const char* block_end = cursor;
            agnc_md_line_buf_t* block_lines = NULL;
            const char* block_ptrs[MD_MAX_ROWS] = {0};
            int block_count = 0;

            block_lines = (agnc_md_line_buf_t*)calloc((size_t)MD_MAX_ROWS, sizeof(agnc_md_line_buf_t));
            if (block_lines == NULL) {
                continue;
            }

            while (block_end && *block_end && block_count < MD_MAX_ROWS) {
                const char* next_end = strchr(block_end, '\n');
                size_t blen = next_end ? (size_t)(next_end - block_end) : strlen(block_end);
                if (blen >= sizeof(block_lines[0])) blen = sizeof(block_lines[0]) - 1;
                memcpy(block_lines[block_count], block_end, blen);
                block_lines[block_count][blen] = '\0';
                if (!is_table_row_line(block_lines[block_count]) && !is_table_separator_line(block_lines[block_count])) {
                    break;
                }
                block_ptrs[block_count] = block_lines[block_count];
                block_count++;
                block_end = next_end ? next_end + 1 : block_end + blen;
            }

            render_table_block(block_ptrs, block_count, 0);
            putchar('\n');
            free(block_lines);
            paragraph_continue = 0;
            cursor = block_end;
            continue;
        }

        if (is_box_drawing_line(line)) {
            const char* block_end = cursor;
            agnc_md_line_buf_t* block_lines = NULL;
            const char* block_ptrs[96] = {0};
            int block_count = 0;

            block_lines = (agnc_md_line_buf_t*)calloc(96, sizeof(agnc_md_line_buf_t));
            if (block_lines == NULL) {
                continue;
            }

            while (block_end && *block_end && block_count < 96) {
                const char* next_end = strchr(block_end, '\n');
                size_t blen = next_end ? (size_t)(next_end - block_end) : strlen(block_end);
                if (blen >= sizeof(block_lines[0])) blen = sizeof(block_lines[0]) - 1;
                memcpy(block_lines[block_count], block_end, blen);
                block_lines[block_count][blen] = '\0';
                if (block_lines[block_count][0] == '\0') break;
                if (!is_box_drawing_line(block_lines[block_count])) break;
                block_ptrs[block_count] = block_lines[block_count];
                block_count++;
                block_end = next_end ? next_end + 1 : block_end + blen;
            }
            render_preformatted_block(block_ptrs, block_count, time_prefix, &first_line, continuation_indent);
            free(block_lines);
            paragraph_continue = 0;
            cursor = block_end;
            continue;
        }

        if (line[0] == '\0') {
            paragraph_continue = 0;
            if (!first_line) putchar('\n');
        } else {
            render_prefixed_line(time_prefix, &first_line, paragraph_continue, continuation_indent, line);
            paragraph_continue = 1;
        }

        cursor = line_end ? line_end + 1 : cursor + line_len;
    }
    fflush(stdout);
}


