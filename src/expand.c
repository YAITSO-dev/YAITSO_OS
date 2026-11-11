#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

static int is_name_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_name_char(char c)  { return isalnum((unsigned char)c) || c == '_'; }
static int all_digits(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

char *expand_vars(struct yaitso_ctx *c, const char *s, int *status)
{
    if (!s) return NULL;
    size_t cap = strlen(s) + 64;
    char *out = xmalloc(cap);
    size_t o = 0;

    const char *p = s;
    while (*p) {
        if (*p != '$') {
            if (o + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
            out[o++] = *p++;
            continue;
        }
        p++;
        if (*p == '{') {
            p++;
            const char *e = strchr(p, '}');
            if (!e) {
                if (o + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                out[o++] = '$';
                continue;
            }
            char name[128];
            size_t nl = (size_t)(e - p);
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, p, nl);
            name[nl] = 0;
            const char *v;
            if (all_digits(name)) v = posarg_get(c, atoi(name));
            else if (!strcmp(name, "#")) {
                static char nbuf[32];
                snprintf(nbuf, sizeof(nbuf), "%d",
                         c->posargc > 0 ? c->posargc - 1 : 0);
                v = nbuf;
            } else v = env_get(c, name);
            if (v) {
                size_t vl = strlen(v);
                while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                memcpy(out + o, v, vl);
                o += vl;
            }
            p = e + 1;
            continue;
        }
        if (*p == '?') {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", c->last_status);
            size_t vl = strlen(buf);
            while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
            memcpy(out + o, buf, vl);
            o += vl;
            p++;
            continue;
        }
        if (*p == '$') {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", (int)getpid());
            size_t vl = strlen(buf);
            while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
            memcpy(out + o, buf, vl);
            o += vl;
            p++;
            continue;
        }
        if (*p == '!') {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", (int)c->last_bg_pid);
            size_t vl = strlen(buf);
            while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
            memcpy(out + o, buf, vl);
            o += vl;
            p++;
            continue;
        }
        if (*p == '#') {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d",
                     c->posargc > 0 ? c->posargc - 1 : 0);
            size_t vl = strlen(buf);
            while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
            memcpy(out + o, buf, vl);
            o += vl;
            p++;
            continue;
        }
        if (*p == '@' || *p == '*') {
            p++;
            for (int i = 1; i < c->posargc; i++) {
                if (i > 1) {
                    while (o + 2 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                    out[o++] = ' ';
                }
                const char *v = c->posargs[i];
                size_t vl = strlen(v);
                while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                memcpy(out + o, v, vl);
                o += vl;
            }
            continue;
        }
        if (*p >= '0' && *p <= '9') {
            char buf[8];
            buf[0] = *p;
            buf[1] = 0;
            const char *v = posarg_get(c, buf[0] - '0');
            if (v) {
                size_t vl = strlen(v);
                while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                memcpy(out + o, v, vl);
                o += vl;
            }
            p++;
            continue;
        }
        if (is_name_start(*p)) {
            const char *st = p;
            while (is_name_char(*p)) p++;
            size_t nl = (size_t)(p - st);
            char name[256];
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, st, nl);
            name[nl] = 0;
            const char *v = env_get(c, name);
            if (v) {
                size_t vl = strlen(v);
                while (o + vl + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
                memcpy(out + o, v, vl);
                o += vl;
            }
            continue;
        }
        if (o + 1 >= cap) { cap *= 2; out = xrealloc(out, cap); }
        out[o++] = '$';
    }
    out[o] = 0;
    if (status) *status = 0;
    return out;
}
