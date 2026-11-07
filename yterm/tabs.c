#define _GNU_SOURCE
#include "yterm.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

void yt_session_init(struct yt_session *s, int cols, int rows)
{
    memset(s, 0, sizeof(*s));
    s->active = -1;
    s->next_id = 1;
    (void)cols; (void)rows;
}

int yt_session_new_tab(struct yt_session *s, char *const argv[],
                       int cols, int rows)
{
    if (s->ntabs >= YT_MAX_TABS) return -1;

    int slot = s->ntabs;
    struct yt_tab *t = &s->tabs[slot];
    memset(t, 0, sizeof(*t));
    t->master_fd = -1;

    yt_term_init(&t->term, cols, rows);
    if (yt_pty_spawn(&t->term, argv, &t->master_fd) != 0) return -1;

    t->alive = 1;
    t->id = s->next_id++;
    const char *base = (argv && argv[0]) ? argv[0] : "shell";
    const char *slash = strrchr(base, '/');
    snprintf(t->title, sizeof(t->title), "%s", slash ? slash + 1 : base);

    s->ntabs++;
    s->active = slot;
    return slot;
}

void yt_session_close_tab(struct yt_session *s, int idx)
{
    if (idx < 0 || idx >= s->ntabs) return;

    struct yt_tab *t = &s->tabs[idx];
    if (t->master_fd >= 0) close(t->master_fd);
    t->master_fd = -1;
    t->alive = 0;

    for (int i = idx; i < s->ntabs - 1; i++)
        s->tabs[i] = s->tabs[i + 1];
    s->ntabs--;

    if (s->ntabs == 0) { s->active = -1; return; }
    if (s->active >= s->ntabs) s->active = s->ntabs - 1;
    if (s->active < 0) s->active = 0;
}

void yt_session_switch(struct yt_session *s, int idx)
{
    if (idx < 0 || idx >= s->ntabs) return;
    s->active = idx;
}

void yt_bell_notify(const char *tab_title)
{
    (void)tab_title;
}
