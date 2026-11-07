#include "yterm.h"
#include <string.h>

static void draw_cell(ywt_buf_t *b, ywt_font_t *f, struct yt_cell *c,
                      int px, int py)
{
    uint32_t fg = c->fg;
    uint32_t bg = c->bg;
    if (c->attrs & YT_ATTR_REV) {
        uint32_t t = fg; fg = bg; bg = t;
    }
    if (c->attrs & YT_ATTR_DIM) {
        fg = ((fg & 0xfefefefe) >> 1) | 0xff000000u;
    }
    ywt_draw_rect(b, px, py, YT_CELL_W, YT_CELL_H, bg);
    if (c->attrs & YT_ATTR_UNDER)
        ywt_draw_rect(b, px, py + YT_CELL_H - 2, YT_CELL_W, 1, fg);
    if (c->cp == ' ' || c->cp == 0) return;
    ywt_font_glyph(f, b, c->cp, px, py, fg);
}

static void render_blank_line(ywt_buf_t *b, int cols, int row, uint32_t bg)
{
    for (int x = 0; x < cols; x++)
        ywt_draw_rect(b, x * YT_CELL_W, row * YT_CELL_H,
                      YT_CELL_W, YT_CELL_H, bg);
}

static void render_hist_line(struct yt_term *t, ywt_buf_t *b, ywt_font_t *f,
                             int hist_idx, int row)
{
    struct yt_line *l = &t->hist[hist_idx];
    for (int x = 0; x < t->cols; x++)
        draw_cell(b, f, &l->cells[x], x * YT_CELL_W, row * YT_CELL_H);
}

static void render_screen_line(struct yt_term *t, ywt_buf_t *b, ywt_font_t *f,
                               int row)
{
    struct yt_line *l = &t->screen[row];
    for (int x = 0; x < t->cols; x++)
        draw_cell(b, f, &l->cells[x], x * YT_CELL_W, row * YT_CELL_H);
}

void yt_term_render(struct yt_term *t, ywt_buf_t *b, ywt_font_t *f)
{
    ywt_buf_clear(b, t->cur_bg);

    int so = t->scroll_off;
    if (so > t->hist_count) so = t->hist_count;
    if (so > t->rows) so = t->rows;

    int hist_start = t->hist_head - t->hist_count;
    if (hist_start < 0) hist_start += YT_HIST_MAX;

    for (int row = 0; row < t->rows; row++) {
        if (row < so) {
            int idx = t->hist_count - so + row;
            if (idx < 0 || idx >= t->hist_count) {
                render_blank_line(b, t->cols, row, t->cur_bg);
            } else {
                int hi = (hist_start + idx) % YT_HIST_MAX;
                render_hist_line(t, b, f, hi, row);
            }
        } else {
            int srow = row - so;
            if (srow < t->rows)
                render_screen_line(t, b, f, srow);
            else
                render_blank_line(b, t->cols, row, t->cur_bg);
        }
    }

    if (t->cursor_visible && so == 0) {
        int px = t->cx * YT_CELL_W;
        int py = t->cy * YT_CELL_H;
        ywt_draw_rect_blend(b, px, py, YT_CELL_W, YT_CELL_H, 0x55ffffff);
        ywt_draw_rect(b, px, py + YT_CELL_H - 2, YT_CELL_W, 2, YT_COLOR_CUR);
    }
}
