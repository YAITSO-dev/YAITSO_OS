#define _GNU_SOURCE
#include "yterm.h"
#include <string.h>

static int match_line(struct yt_line *l, const char *q, int qlen)
{
    if (qlen <= 0) return 0;
    for (int i = 0; i <= l->len - qlen; i++) {
        int ok = 1;
        for (int j = 0; j < qlen; j++) {
            uint32_t a = l->cells[i + j].cp;
            uint32_t b = (uint32_t)(unsigned char)q[j];
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

void yt_search_start(struct yt_search *s)
{
    s->active = 1;
    s->qlen = 0;
    s->query[0] = 0;
    s->nmatch = 0;
    s->cur_match = -1;
    s->from_hist = 0;
}

void yt_search_stop(struct yt_search *s)
{
    s->active = 0;
    s->nmatch = 0;
    s->cur_match = -1;
}

void yt_search_find(struct yt_search *s, struct yt_term *t)
{
    s->nmatch = 0;
    s->cur_match = -1;
    if (s->qlen == 0) return;

    for (int row = 0; row < t->rows; row++) {
        if (match_line(&t->screen[row], s->query, s->qlen))
            s->matches[s->nmatch++] = row;
    }
    if (s->nmatch > 0) s->cur_match = 0;
}

void yt_search_input(struct yt_search *s, struct yt_term *t, uint32_t cp)
{
    if (cp == 0x7f || cp == 0x08) {
        if (s->qlen > 0) s->query[--s->qlen] = 0;
    } else if (cp >= 0x20 && cp < 0x7f) {
        if (s->qlen < YT_SEARCH_MAX - 1) {
            s->query[s->qlen++] = (char)cp;
            s->query[s->qlen] = 0;
        }
    }
    yt_search_find(s, t);
}

void yt_search_next(struct yt_search *s, struct yt_term *t, int dir)
{
    (void)t;
    if (s->nmatch == 0) return;
    s->cur_match = (s->cur_match + dir + s->nmatch) % s->nmatch;
}
