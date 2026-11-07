#include "ywt-internal.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define YWT_CACHE_SLOTS 512

struct ywt_glyph {
    uint32_t  cp;
    int       w, h, left, top, pitch;
    int       advance;
    uint8_t  *bitmap;
    struct ywt_glyph *next;
};

struct ywt_font {
    FT_Face           face;
    int               size;
    int               cw;
    int               ascent;
    int               descent;
    int               line_h;
    struct ywt_glyph *cache[YWT_CACHE_SLOTS];
};

static FT_Library g_ft = NULL;
static int        g_ft_ok = 0;

static void ensure_ft(void)
{
    if (g_ft_ok) return;
    if (FT_Init_FreeType(&g_ft) == 0) g_ft_ok = 1;
}

ywt_font_t *ywt_font_load(const char *path, int size)
{
    ensure_ft();
    if (!g_ft_ok || !path) return NULL;
    ywt_font_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    if (FT_New_Face(g_ft, path, 0, &f->face)) { free(f); return NULL; }
    if (FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)size)) {
        FT_Done_Face(f->face);
        free(f);
        return NULL;
    }
    f->size = size;
    if (FT_Load_Char(f->face, 'M', FT_LOAD_RENDER) == 0)
        f->cw = (int)(f->face->glyph->advance.x >> 6);
    if (f->cw <= 0) f->cw = size / 2 + 1;
    f->ascent  = (int)(f->face->size->metrics.ascender >> 6);
    f->descent = (int)(-f->face->size->metrics.descender >> 6);
    f->line_h  = (int)(f->face->size->metrics.height >> 6);
    if (f->line_h <= 0) f->line_h = f->ascent + f->descent + 2;
    return f;
}

void ywt_font_destroy(ywt_font_t *f)
{
    if (!f) return;
    for (int i = 0; i < YWT_CACHE_SLOTS; i++) {
        struct ywt_glyph *g = f->cache[i];
        while (g) {
            struct ywt_glyph *nx = g->next;
            free(g->bitmap);
            free(g);
            g = nx;
        }
    }
    if (f->face) FT_Done_Face(f->face);
    free(f);
}

int ywt_font_width(ywt_font_t *f)  { return f ? f->cw : 0; }
int ywt_font_ascent(ywt_font_t *f) { return f ? f->ascent : 0; }
int ywt_font_height(ywt_font_t *f) { return f ? f->line_h : 0; }

int ywt_text_width(ywt_font_t *f, const char *s)
{
    if (!f || !s) return 0;
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        if      (*p < 0x80)          p += 1;
        else if ((*p & 0xe0) == 0xc0) p += 2;
        else if ((*p & 0xf0) == 0xe0) p += 3;
        else if ((*p & 0xf8) == 0xf0) p += 4;
        else                          p += 1;
        n++;
    }
    return n * f->cw;
}

static struct ywt_glyph *glyph_get(ywt_font_t *f, uint32_t cp)
{
    uint32_t idx = cp % YWT_CACHE_SLOTS;
    for (struct ywt_glyph *g = f->cache[idx]; g; g = g->next)
        if (g->cp == cp) return g;

    if (FT_Load_Char(f->face, cp, FT_LOAD_RENDER)) return NULL;
    FT_GlyphSlot src = f->face->glyph;

    struct ywt_glyph *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->cp      = cp;
    g->w       = (int)src->bitmap.width;
    g->h       = (int)src->bitmap.rows;
    g->pitch   = src->bitmap.pitch;
    g->left    = src->bitmap_left;
    g->top     = src->bitmap_top;
    g->advance = (int)(src->advance.x >> 6);
    if (g->w > 0 && g->h > 0 && g->pitch > 0) {
        size_t sz = (size_t)g->pitch * (size_t)g->h;
        g->bitmap = malloc(sz);
        if (g->bitmap) memcpy(g->bitmap, src->bitmap.buffer, sz);
    }
    g->next = f->cache[idx];
    f->cache[idx] = g;
    return g;
}

void ywt_font_glyph(ywt_font_t *f, ywt_buf_t *b, uint32_t cp,
                    int x, int y, uint32_t argb)
{
    if (!f || !b) return;
    struct ywt_glyph *g = glyph_get(f, cp);
    if (!g || !g->bitmap) return;

    uint32_t *px = ywt_buf_data(b);
    int bw = ywt_buf_width(b);
    int bh = ywt_buf_height(b);

    int base = y + f->ascent;
    int ox   = x + g->left;
    int oy   = base - g->top;

    uint32_t fg_a = (argb >> 24) & 0xff;
    uint32_t fg_r = (argb >> 16) & 0xff;
    uint32_t fg_g = (argb >>  8) & 0xff;
    uint32_t fg_b = (argb >>  0) & 0xff;

    for (int j = 0; j < g->h; j++) {
        int py = oy + j;
        if (py < 0 || py >= bh) continue;
        for (int i = 0; i < g->w; i++) {
            int pxx = ox + i;
            if (pxx < 0 || pxx >= bw) continue;
            uint8_t a = g->bitmap[j * g->pitch + i];
            if (!a) continue;
            uint32_t *d = px + (size_t)py * (size_t)bw + (size_t)pxx;
            uint32_t bg = *d;
            uint32_t bg_r = (bg >> 16) & 0xff;
            uint32_t bg_g = (bg >>  8) & 0xff;
            uint32_t bg_b = (bg >>  0) & 0xff;

            uint32_t sa = (fg_a * a + 127) / 255;
            uint32_t r  = (fg_r * sa + bg_r * (255 - sa)) / 255;
            uint32_t gg = (fg_g * sa + bg_g * (255 - sa)) / 255;
            uint32_t bb = (fg_b * sa + bg_b * (255 - sa)) / 255;
            *d = 0xff000000u | (r << 16) | (gg << 8) | bb;
        }
    }
}
