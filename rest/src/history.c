#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define HIST_RING 4096

static char *g_ring[HIST_RING];
static int   g_ring_head = 0;
static int   g_ring_count = 0;

void history_init(struct yaitso_ctx *c, int size, const char *path)
{
    (void)size;
    if (c->histfile) xfree(c->histfile);
    c->histfile = path ? xstrdup(path) : NULL;
    c->hist_count = 0;
    c->hist_pos = 0;
    g_ring_head = 0;
    g_ring_count = 0;

    if (!c->histfile) return;
    FILE *f = fopen(c->histfile, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) {
            xfree(g_ring[g_ring_head]);
            g_ring[g_ring_head] = xstrdup(line);
            g_ring_head = (g_ring_head + 1) % HIST_RING;
            if (g_ring_count < HIST_RING) g_ring_count++;
        }
    }
    fclose(f);
}

void history_free(struct yaitso_ctx *c)
{
    (void)c;
    for (int i = 0; i < HIST_RING; i++) {
        xfree(g_ring[i]);
        g_ring[i] = NULL;
    }
    g_ring_head = 0;
    g_ring_count = 0;
}

void history_add(struct yaitso_ctx *c, const char *line)
{
    (void)c;
    if (!line || !line[0]) return;
    if (g_ring_count > 0) {
        int last = (g_ring_head - 1 + HIST_RING) % HIST_RING;
        if (g_ring[last] && !strcmp(g_ring[last], line)) return;
    }
    xfree(g_ring[g_ring_head]);
    g_ring[g_ring_head] = xstrdup(line);
    g_ring_head = (g_ring_head + 1) % HIST_RING;
    if (g_ring_count < HIST_RING) g_ring_count++;
}

const char *history_get(struct yaitso_ctx *c, int idx)
{
    (void)c;
    if (idx < 0 || idx >= g_ring_count) return NULL;
    int start = (g_ring_head - g_ring_count + HIST_RING) % HIST_RING;
    return g_ring[(start + idx) % HIST_RING];
}

int history_count(void) { return g_ring_count; }

void history_save(struct yaitso_ctx *c)
{
    if (!c->histfile) return;
    FILE *f = fopen(c->histfile, "w");
    if (!f) return;
    int start = (g_ring_head - g_ring_count + HIST_RING) % HIST_RING;
    for (int i = 0; i < g_ring_count; i++) {
        const char *l = g_ring[(start + i) % HIST_RING];
        if (l) fprintf(f, "%s\n", l);
    }
    fclose(f);
    chmod(c->histfile, 0600);
}

void history_clear(struct yaitso_ctx *c)
{
    (void)c;
    for (int i = 0; i < HIST_RING; i++) {
        xfree(g_ring[i]);
        g_ring[i] = NULL;
    }
    g_ring_head = 0;
    g_ring_count = 0;
}
