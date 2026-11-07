#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "yaitso: out of memory\n"); exit(1); }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "yaitso: out of memory\n"); exit(1); }
    return q;
}

char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void xfree(void *p)
{
    free(p);
}

char *trim(char *s)
{
    if (!s) return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

int is_blank(const char *s)
{
    if (!s) return 1;
    while (*s) {
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return 0;
        s++;
    }
    return 1;
}

int split_words(const char *s, char ***out, int *outn)
{
    if (!s || !out || !outn) return -1;
    *out = NULL;
    *outn = 0;

    int cap = 16;
    char **arr = xmalloc(sizeof(char *) * (size_t)cap);
    int n = 0;

    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        size_t bufcap = 64, buflen = 0;
        char *buf = xmalloc(bufcap);

        int in_sq = 0, in_dq = 0;
        while (*p) {
            char c = *p;
            if (!in_sq && !in_dq && (c == ' ' || c == '\t')) break;

            if (in_sq) {
                if (c == '\'') { in_sq = 0; p++; continue; }
                if (buflen + 1 >= bufcap) { bufcap *= 2; buf = xrealloc(buf, bufcap); }
                buf[buflen++] = c;
                p++;
                continue;
            }
            if (in_dq) {
                if (c == '"') { in_dq = 0; p++; continue; }
                if (c == '\\' && p[1] == '"') {
                    if (buflen + 1 >= bufcap) { bufcap *= 2; buf = xrealloc(buf, bufcap); }
                    buf[buflen++] = '"';
                    p += 2;
                    continue;
                }
                if (buflen + 1 >= bufcap) { bufcap *= 2; buf = xrealloc(buf, bufcap); }
                buf[buflen++] = c;
                p++;
                continue;
            }
            if (c == '\'') { in_sq = 1; p++; continue; }
            if (c == '"')  { in_dq = 1; p++; continue; }
            if (c == '\\' && p[1]) {
                if (buflen + 1 >= bufcap) { bufcap *= 2; buf = xrealloc(buf, bufcap); }
                buf[buflen++] = p[1];
                p += 2;
                continue;
            }
            if (buflen + 1 >= bufcap) { bufcap *= 2; buf = xrealloc(buf, bufcap); }
            buf[buflen++] = c;
            p++;
        }

        if (n >= cap) {
            cap *= 2;
            arr = xrealloc(arr, sizeof(char *) * (size_t)cap);
        }
        buf[buflen] = 0;
        arr[n++] = buf;
    }

    *out = arr;
    *outn = n;
    return 0;
}

int parse_redirect(const char *s, char **clean, char **infile,
                   char **outfile, int *append, char **errfile,
                   int *errappend)
{
    if (!s || !clean) return -1;
    *clean = NULL;
    if (infile)    *infile    = NULL;
    if (outfile)   *outfile   = NULL;
    if (append)    *append    = 0;
    if (errfile)   *errfile   = NULL;
    if (errappend) *errappend = 0;

    size_t n = strlen(s);
    char *buf = xmalloc(n + 1);
    size_t o = 0;
    int changed = 0;

    for (size_t i = 0; i < n; ) {
        if (i + 2 < n && s[i] == '2' && s[i+1] == '>' && s[i+2] == '>') {
            i += 3;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (errfile) { xfree(*errfile); *errfile = xstrndup(s + st, i - st); }
                if (errappend) *errappend = 1;
                changed = 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '2' && s[i+1] == '>') {
            i += 2;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (errfile) { xfree(*errfile); *errfile = xstrndup(s + st, i - st); }
                changed = 1;
                continue;
            }
        }
        if (i + 2 < n && s[i] == '&' && s[i+1] == '>' && s[i+2] == '>') {
            i += 3;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (outfile) { xfree(*outfile); *outfile = xstrndup(s + st, i - st); }
                if (append)  *append = 1;
                if (errfile) { xfree(*errfile); *errfile = xstrdup(*outfile); }
                if (errappend) *errappend = 1;
                changed = 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '&' && s[i+1] == '>') {
            i += 2;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (outfile) { xfree(*outfile); *outfile = xstrndup(s + st, i - st); }
                if (errfile) { xfree(*errfile); *errfile = xstrdup(*outfile); }
                changed = 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '>' && s[i+1] == '>') {
            i += 2;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (outfile) { xfree(*outfile); *outfile = xstrndup(s + st, i - st); }
                if (append)  *append = 1;
                changed = 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '>' ) {
            i += 1;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (outfile) { xfree(*outfile); *outfile = xstrndup(s + st, i - st); }
                changed = 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '<') {
            i += 1;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            size_t st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ';' &&
                   s[i] != '|' && s[i] != '&') i++;
            if (i > st) {
                if (infile) { xfree(*infile); *infile = xstrndup(s + st, i - st); }
                changed = 1;
                continue;
            }
        }

        buf[o++] = s[i++];
    }
    buf[o] = 0;
    *clean = buf;
    return changed;
}

void print_version(void)
{
    printf("Yaitso OS %s\n", YAITSO_VERSION);
}

void print_help(void)
{
    printf("Yaitso OS %s — встроенные команды:\n", YAITSO_VERSION);
    printf("  help version about welcome clear reset echo printf true false\n");
    printf("  date uname whoami history sleep time set export unset env\n");
    printf("  alias unalias which type jobs fg bg wait shift\n");
    printf("  cd pwd ls cat head tail wc grep seq\n");
    printf("  pushd popd dirs source read exit logout\n");
    printf("Разделители: ; && || |  Редиректы: < > >> 2> 2>> &> &>>\n");
}
