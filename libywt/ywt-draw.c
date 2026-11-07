#include "ywt-internal.h"
#include <string.h>
#include <stdint.h>

static inline uint32_t blend(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xff;
    if (sa == 0)   return dst;
    if (sa == 255) return src;
    uint32_t sr = (src >> 16) & 0xff, sg = (src >> 8) & 0xff, sb = src & 0xff;
    uint32_t dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
    uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint32_t b = (sb * sa + db * (255 - sa)) / 255;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static int clip_rect(ywt_buf_t *b, int *x, int *y, int *w, int *h)
{
    int bw = ywt_buf_width(b);
    int bh = ywt_buf_height(b);
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > bw) *w = bw - *x;
    if (*y + *h > bh) *h = bh - *y;
    return *w > 0 && *h > 0;
}

void ywt_draw_rect(ywt_buf_t *b, int x, int y, int w, int h, uint32_t argb)
{
    if (!b) return;
    if (!clip_rect(b, &x, &y, &w, &h)) return;
    uint32_t *px = ywt_buf_data(b);
    int bw = ywt_buf_width(b);
    for (int j = 0; j < h; j++) {
        uint32_t *row = px + (size_t)(y + j) * (size_t)bw + (size_t)x;
        for (int i = 0; i < w; i++) row[i] = argb;
    }
}

void ywt_draw_rect_blend(ywt_buf_t *b, int x, int y, int w, int h, uint32_t argb)
{
    if (!b) return;
    if (!clip_rect(b, &x, &y, &w, &h)) return;
    uint32_t *px = ywt_buf_data(b);
    int bw = ywt_buf_width(b);
    for (int j = 0; j < h; j++) {
        uint32_t *row = px + (size_t)(y + j) * (size_t)bw + (size_t)x;
        for (int i = 0; i < w; i++) row[i] = blend(row[i], argb);
    }
}

void ywt_draw_hline(ywt_buf_t *b, int x, int y, int w, uint32_t argb)
{
    ywt_draw_rect(b, x, y, w, 1, argb);
}

void ywt_draw_vline(ywt_buf_t *b, int x, int y, int h, uint32_t argb)
{
    ywt_draw_rect(b, x, y, 1, h, argb);
}

void ywt_draw_text(ywt_buf_t *b, ywt_font_t *f, const char *s,
                   int x, int y, uint32_t argb)
{
    if (!b || !f || !s) return;
    int pen = x;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp = *p;
        int n = 0;
        if (cp < 0x80) { n = 0; }
        else if ((cp & 0xe0) == 0xc0) { cp &= 0x1f; n = 1; }
        else if ((cp & 0xf0) == 0xe0) { cp &= 0x0f; n = 2; }
        else if ((cp & 0xf8) == 0xf0) { cp &= 0x07; n = 3; }
        else { p++; continue; }
        for (int i = 0; i < n && p[1]; i++) {
            p++;
            cp = (cp << 6) | (*p & 0x3f);
        }
        ywt_font_glyph(f, b, cp, pen, y, argb);
        pen += ywt_font_width(f);
        p++;
    }
}
