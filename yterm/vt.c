#include "yterm.h"
#include <string.h>
#include <stdlib.h>

enum {
    ST_GROUND = 0,
    ST_ESC,
    ST_CSI_ENTRY,
    ST_CSI_PARAM,
    ST_CSI_INTERMED,
    ST_OSC,
    ST_OSC_ESC,
};

static void line_clear(struct yt_line *l, uint32_t bg)
{
    for (int i = 0; i < YT_COLS_MAX; i++) {
        l->cells[i].cp    = ' ';
        l->cells[i].fg    = YT_COLOR_FG_DEF;
        l->cells[i].bg    = bg;
        l->cells[i].attrs = 0;
    }
    l->len = 0;
}

static void line_clear_range(struct yt_line *l, int from, int to, uint32_t bg)
{
    if (from < 0) from = 0;
    if (to > YT_COLS_MAX) to = YT_COLS_MAX;
    for (int i = from; i < to; i++) {
        l->cells[i].cp    = ' ';
        l->cells[i].fg    = YT_COLOR_FG_DEF;
        l->cells[i].bg    = bg;
        l->cells[i].attrs = 0;
    }
}

void yt_term_init(struct yt_term *t, int cols, int rows)
{
    memset(t, 0, sizeof(*t));
    t->cols = cols > YT_COLS_MAX ? YT_COLS_MAX : cols;
    t->rows = rows > YT_ROWS_MAX ? YT_ROWS_MAX : rows;
    t->cursor_visible = 1;
    t->cur_fg = YT_COLOR_FG_DEF;
    t->cur_bg = YT_COLOR_BG_DEF;
    t->cur_attrs = 0;
    t->state = ST_GROUND;
    for (int i = 0; i < YT_ROWS_MAX; i++) line_clear(&t->screen[i], t->cur_bg);
    for (int i = 0; i < YT_HIST_MAX; i++) line_clear(&t->hist[i], t->cur_bg);
}

void yt_term_resize(struct yt_term *t, int cols, int rows)
{
    if (cols > YT_COLS_MAX) cols = YT_COLS_MAX;
    if (rows > YT_ROWS_MAX) rows = YT_ROWS_MAX;
    if (cols == t->cols && rows == t->rows) return;

    if (cols < t->cols) {
        for (int i = 0; i < t->rows; i++)
            line_clear_range(&t->screen[i], cols, t->cols, t->cur_bg);
    }
    t->cols = cols;
    t->rows = rows;
    if (t->cx >= cols) t->cx = cols - 1;
    if (t->cy >= rows) t->cy = rows - 1;
}

void yt_term_scroll_up(struct yt_term *t)
{
    struct yt_line tmp = t->screen[0];
    for (int i = 1; i < t->rows; i++) t->screen[i - 1] = t->screen[i];
    t->screen[t->rows - 1] = tmp;
    line_clear(&t->screen[t->rows - 1], t->cur_bg);
}

void yt_term_scroll_down(struct yt_term *t)
{
    struct yt_line tmp = t->screen[t->rows - 1];
    for (int i = t->rows - 1; i > 0; i--) t->screen[i] = t->screen[i - 1];
    t->screen[0] = tmp;
    line_clear(&t->screen[0], t->cur_bg);
}

static void push_history(struct yt_term *t, struct yt_line *l)
{
    t->hist[t->hist_head] = *l;
    t->hist_head = (t->hist_head + 1) % YT_HIST_MAX;
    if (t->hist_count < YT_HIST_MAX) t->hist_count++;
}

int yt_term_scroll_history(struct yt_term *t, int delta)
{
    int prev = t->scroll_off;
    t->scroll_off += delta;
    if (t->scroll_off < 0) t->scroll_off = 0;
    if (t->scroll_off > t->hist_count) t->scroll_off = t->hist_count;
    return t->scroll_off - prev;
}

static void scroll_up_hist(struct yt_term *t)
{
    push_history(t, &t->screen[0]);
    for (int i = 1; i < t->rows; i++) t->screen[i - 1] = t->screen[i];
    line_clear(&t->screen[t->rows - 1], t->cur_bg);
}

static void put_char(struct yt_term *t, uint32_t cp)
{
    if (t->cx >= t->cols) {
        t->cx = 0;
        t->cy++;
        if (t->cy >= t->rows) {
            scroll_up_hist(t);
            t->cy = t->rows - 1;
        }
    }
    if (t->cy >= 0 && t->cy < YT_ROWS_MAX) {
        struct yt_cell *c = &t->screen[t->cy].cells[t->cx];
        c->cp    = cp;
        c->fg    = t->cur_fg;
        c->bg    = t->cur_bg;
        c->attrs = t->cur_attrs;
        if (t->cx + 1 > t->screen[t->cy].len)
            t->screen[t->cy].len = t->cx + 1;
    }
    t->cx++;
}

static void sgr_reset(struct yt_term *t)
{
    t->cur_fg = YT_COLOR_FG_DEF;
    t->cur_bg = YT_COLOR_BG_DEF;
    t->cur_attrs = 0;
}

static uint32_t ansi256(int n)
{
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    if (n < 16) {
        static const uint32_t base[16] = {
            0xff2e3440, 0xffbf616a, 0xffa3be8c, 0xffebcb8b,
            0xff81a1c1, 0xffb48ead, 0xff88c0d0, 0xffe5e9f0,
            0xff4c566a, 0xffbf616a, 0xffa3be8c, 0xffebcb8b,
            0xff81a1c1, 0xffb48ead, 0xff8fbcbb, 0xffeceff4,
        };
        return base[n];
    }
    if (n < 232) {
        n -= 16;
        int r = n / 36;
        int g = (n / 6) % 6;
        int b = n % 6;
        static const int lv[6] = {0, 95, 135, 175, 215, 255};
        return 0xff000000u | ((uint32_t)lv[r] << 16) |
                             ((uint32_t)lv[g] << 8) | (uint32_t)lv[b];
    }
    int gray = 8 + (n - 232) * 10;
    return 0xff000000u | ((uint32_t)gray << 16) |
                         ((uint32_t)gray << 8) | (uint32_t)gray;
}

static void sgr(struct yt_term *t, const int *args, int n)
{
    if (n == 0) { sgr_reset(t); return; }
    for (int i = 0; i < n; i++) {
        int a = args[i];
        switch (a) {
        case 0: sgr_reset(t); break;
        case 1: t->cur_attrs |= YT_ATTR_BOLD; break;
        case 2: t->cur_attrs |= YT_ATTR_DIM; break;
        case 3: t->cur_attrs |= YT_ATTR_ITALIC; break;
        case 4: t->cur_attrs |= YT_ATTR_UNDER; break;
        case 7: t->cur_attrs |= YT_ATTR_REV; break;
        case 22: t->cur_attrs &= (uint16_t)~(YT_ATTR_BOLD | YT_ATTR_DIM); break;
        case 23: t->cur_attrs &= (uint16_t)~YT_ATTR_ITALIC; break;
        case 24: t->cur_attrs &= (uint16_t)~YT_ATTR_UNDER; break;
        case 27: t->cur_attrs &= (uint16_t)~YT_ATTR_REV; break;
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            t->cur_fg = ansi256(a - 30);
            break;
        case 38:
            if (i + 1 < n && args[i + 1] == 5 && i + 2 < n) {
                t->cur_fg = ansi256(args[i + 2]);
                i += 2;
            } else if (i + 1 < n && args[i + 1] == 2 && i + 4 < n) {
                uint32_t r = (uint32_t)args[i + 2] & 0xff;
                uint32_t g = (uint32_t)args[i + 3] & 0xff;
                uint32_t b = (uint32_t)args[i + 4] & 0xff;
                t->cur_fg = 0xff000000u | (r << 16) | (g << 8) | b;
                i += 4;
            }
            break;
        case 39: t->cur_fg = YT_COLOR_FG_DEF; break;
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            t->cur_bg = ansi256(a - 40);
            break;
        case 48:
            if (i + 1 < n && args[i + 1] == 5 && i + 2 < n) {
                t->cur_bg = ansi256(args[i + 2]);
                i += 2;
            } else if (i + 1 < n && args[i + 1] == 2 && i + 4 < n) {
                uint32_t r = (uint32_t)args[i + 2] & 0xff;
                uint32_t g = (uint32_t)args[i + 3] & 0xff;
                uint32_t b = (uint32_t)args[i + 4] & 0xff;
                t->cur_bg = 0xff000000u | (r << 16) | (g << 8) | b;
                i += 4;
            }
            break;
        case 49: t->cur_bg = YT_COLOR_BG_DEF; break;
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            t->cur_fg = ansi256(8 + a - 90);
            break;
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            t->cur_bg = ansi256(8 + a - 100);
            break;
        default: break;
        }
    }
}

static void csi_dispatch(struct yt_term *t, char final)
{
    int *a = t->csi_args;
    int n = t->csi_nargs;
    if (n == 0) { a[0] = 0; n = 1; }

    switch (final) {
    case 'm': sgr(t, a, n); break;

    case 'H':
    case 'f': {
        int row = (n >= 1 && a[0] > 0) ? a[0] - 1 : 0;
        int col = (n >= 2 && a[1] > 0) ? a[1] - 1 : 0;
        if (row >= t->rows) row = t->rows - 1;
        if (col >= t->cols) col = t->cols - 1;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        t->cy = row;
        t->cx = col;
        break;
    }
    case 'A': { int k = a[0] > 0 ? a[0] : 1; t->cy -= k; if (t->cy < 0) t->cy = 0; break; }
    case 'B': { int k = a[0] > 0 ? a[0] : 1; t->cy += k; if (t->cy >= t->rows) t->cy = t->rows - 1; break; }
    case 'C': { int k = a[0] > 0 ? a[0] : 1; t->cx += k; if (t->cx >= t->cols) t->cx = t->cols - 1; break; }
    case 'D': { int k = a[0] > 0 ? a[0] : 1; t->cx -= k; if (t->cx < 0) t->cx = 0; break; }
    case 'E': { int k = a[0] > 0 ? a[0] : 1; t->cy += k; t->cx = 0; if (t->cy >= t->rows) t->cy = t->rows - 1; break; }
    case 'F': { int k = a[0] > 0 ? a[0] : 1; t->cy -= k; t->cx = 0; if (t->cy < 0) t->cy = 0; break; }
    case 'G': { int col = a[0] > 0 ? a[0] - 1 : 0; if (col >= t->cols) col = t->cols - 1; t->cx = col; break; }
    case 'd': { int row = a[0] > 0 ? a[0] - 1 : 0; if (row >= t->rows) row = t->rows - 1; t->cy = row; break; }
    case 'J': {
        int mode = a[0];
        if (mode == 2) {
            for (int i = 0; i < t->rows; i++) line_clear(&t->screen[i], t->cur_bg);
        } else if (mode == 0) {
            line_clear_range(&t->screen[t->cy], t->cx, t->cols, t->cur_bg);
            for (int i = t->cy + 1; i < t->rows; i++)
                line_clear(&t->screen[i], t->cur_bg);
        } else if (mode == 1) {
            for (int i = 0; i < t->cy; i++)
                line_clear(&t->screen[i], t->cur_bg);
            line_clear_range(&t->screen[t->cy], 0, t->cx + 1, t->cur_bg);
        }
        break;
    }
    case 'K': {
        int mode = a[0];
        if (mode == 2) line_clear(&t->screen[t->cy], t->cur_bg);
        else if (mode == 0) line_clear_range(&t->screen[t->cy], t->cx, t->cols, t->cur_bg);
        else if (mode == 1) line_clear_range(&t->screen[t->cy], 0, t->cx + 1, t->cur_bg);
        break;
    }
    case 'L': { int k = a[0] > 0 ? a[0] : 1; for (int i = 0; i < k; i++) yt_term_scroll_down(t); break; }
    case 'M': { int k = a[0] > 0 ? a[0] : 1; for (int i = 0; i < k; i++) yt_term_scroll_up(t); break; }
    case 'S': { int k = a[0] > 0 ? a[0] : 1; for (int i = 0; i < k; i++) scroll_up_hist(t); break; }
    case 's': t->saved_x = t->cx; t->saved_y = t->cy; break;
    case 'u': t->cx = t->saved_x; t->cy = t->saved_y; break;
    case 'h':
        if (t->csi_priv && a[0] == 25) t->cursor_visible = 1;
        if (t->csi_priv && a[0] == 2004) t->bracketed_paste = 1;
        break;
    case 'l':
        if (t->csi_priv && a[0] == 25) t->cursor_visible = 0;
        if (t->csi_priv && a[0] == 2004) t->bracketed_paste = 0;
        break;
    default: break;
    }
}

static void osc_dispatch(struct yt_term *t)
{
    t->osc_buf[t->osc_len] = 0;
    (void)t;
}

static void esc_dispatch(struct yt_term *t, char c)
{
    switch (c) {
    case 'M':
        if (t->cy > 0) t->cy--;
        else yt_term_scroll_down(t);
        break;
    case 'D':
        t->cy++;
        if (t->cy >= t->rows) { scroll_up_hist(t); t->cy = t->rows - 1; }
        break;
    case 'E':
        t->cx = 0;
        t->cy++;
        if (t->cy >= t->rows) { scroll_up_hist(t); t->cy = t->rows - 1; }
        break;
    case '7': t->saved_x = t->cx; t->saved_y = t->cy; break;
    case '8': t->cx = t->saved_x; t->cy = t->saved_y; break;
    case 'c': yt_term_init(t, t->cols, t->rows); break;
    default: break;
    }
}

void yt_term_feed(struct yt_term *t, const char *buf, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)buf[i];

        switch (t->state) {
        case ST_GROUND:
            if (c == 0x1b) { t->state = ST_ESC; i++; continue; }
            if (c == '\r') { t->cx = 0; i++; continue; }
            if (c == '\n') {
                t->cy++;
                if (t->cy >= t->rows) { scroll_up_hist(t); t->cy = t->rows - 1; }
                i++; continue;
            }
            if (c == 0x0b || c == 0x0c) {
                t->cy++;
                if (t->cy >= t->rows) { scroll_up_hist(t); t->cy = t->rows - 1; }
                i++; continue;
            }
            if (c == '\t') {
                int next = ((t->cx / 8) + 1) * 8;
                while (t->cx < next && t->cx < t->cols) put_char(t, ' ');
                i++; continue;
            }
            if (c == 0x08) { if (t->cx > 0) t->cx--; i++; continue; }
            if (c == 0x07) { t->bell = 1; i++; continue; }
            if (c < 0x20) { i++; continue; }

            {
                uint32_t cp = c;
                int extra = 0;
                if (c >= 0xf0) { cp = c & 0x07; extra = 3; }
                else if (c >= 0xe0) { cp = c & 0x0f; extra = 2; }
                else if (c >= 0xc0) { cp = c & 0x1f; extra = 1; }
                for (int k = 0; k < extra && i + 1 < n; k++) {
                    i++;
                    cp = (cp << 6) | (buf[i] & 0x3f);
                }
                put_char(t, cp);
                i++;
            }
            continue;

        case ST_ESC:
            if (c == '[') { t->state = ST_CSI_ENTRY; t->csi_nargs = 0;
                            t->csi_cur = 0; t->csi_priv = 0; i++; continue; }
            if (c == ']') { t->state = ST_OSC; t->osc_len = 0; i++; continue; }
            if (c == 'P' || c == '^' || c == '_') { t->state = ST_GROUND; i++; continue; }
            esc_dispatch(t, (char)c);
            t->state = ST_GROUND;
            i++;
            continue;

        case ST_CSI_ENTRY:
            if (c == '?' || c == '>' || c == '<' || c == '=') {
                t->csi_priv = (c == '?');
                t->state = ST_CSI_PARAM;
                i++; continue;
            }
            t->state = ST_CSI_PARAM;
            /* fallthrough */
        case ST_CSI_PARAM:
            if (c >= '0' && c <= '9') {
                if (t->csi_nargs == 0) t->csi_nargs = 1;
                if (t->csi_nargs > 0 && t->csi_nargs <= 16) {
                    t->csi_args[t->csi_nargs - 1] =
                        t->csi_args[t->csi_nargs - 1] * 10 + (c - '0');
                }
                t->csi_cur = 1;
                i++; continue;
            }
            if (c == ';') {
                if (t->csi_nargs == 0) t->csi_nargs = 1;
                if (t->csi_nargs < 16) {
                    t->csi_args[t->csi_nargs] = 0;
                    t->csi_nargs++;
                    t->csi_cur = 0;
                }
                i++; continue;
            }
            if (c >= 0x20 && c <= 0x2f) { t->state = ST_CSI_INTERMED; i++; continue; }
            csi_dispatch(t, (char)c);
            t->state = ST_GROUND;
            i++;
            continue;

        case ST_CSI_INTERMED:
            if (c >= 0x20 && c <= 0x2f) { i++; continue; }
            csi_dispatch(t, (char)c);
            t->state = ST_GROUND;
            i++;
            continue;

        case ST_OSC:
            if (c == 0x07 || c == 0x1b) {
                if (c == 0x1b) { t->state = ST_OSC_ESC; i++; continue; }
                osc_dispatch(t);
                t->state = ST_GROUND;
                i++;
                continue;
            }
            if (t->osc_len < (int)sizeof(t->osc_buf) - 1)
                t->osc_buf[t->osc_len++] = (char)c;
            i++;
            continue;

        case ST_OSC_ESC:
            if (c == '\\') {
                osc_dispatch(t);
                t->state = ST_GROUND;
                i++;
                continue;
            }
            t->state = ST_GROUND;
            i++;
            continue;
        }
    }
}
