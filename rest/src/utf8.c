#define _GNU_SOURCE
#include "yaitso.h"

int utf8_len(const char *s)
{
    if (!s) return 0;
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if      (*p < 0x80)          p += 1;
        else if ((*p & 0xe0) == 0xc0) p += 2;
        else if ((*p & 0xf0) == 0xe0) p += 3;
        else if ((*p & 0xf8) == 0xf0) p += 4;
        else                          p += 1;
        n++;
    }
    return n;
}

int utf8_decode(const char *s, uint32_t *cp)
{
    if (!s || !*s || !cp) return 0;
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        return 2;
    }
    if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 &&
        (p[2] & 0xc0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x0f) << 12) |
              ((uint32_t)(p[1] & 0x3f) << 6) |
               (uint32_t)(p[2] & 0x3f);
        return 3;
    }
    if ((p[0] & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 &&
        (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x07) << 18) |
              ((uint32_t)(p[1] & 0x3f) << 12) |
              ((uint32_t)(p[2] & 0x3f) << 6) |
               (uint32_t)(p[3] & 0x3f);
        return 4;
    }
    *cp = p[0];
    return 1;
}

int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

int utf8_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 32 || cp == 0x7f) return 0;
    if (cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
        (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe30 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) ||
        (cp >= 0x20000 && cp <= 0x2fffd) ||
        (cp >= 0x30000 && cp <= 0x3fffd)))
        return 2;
    return 1;
}
