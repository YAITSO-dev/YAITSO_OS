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

/* Читает слово-цель редиректа, снимая кавычки и экранирование. */
static char *read_redir_word(const char *s, size_t *ip, size_t n)
{
    size_t i = *ip;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t cap = 64, len = 0;
    char *w = xmalloc(cap);
    int sq = 0, dq = 0;
    while (i < n) {
        char c = s[i];
        if (!sq && !dq && (c == ' ' || c == '\t' || c == ';' ||
                           c == '|' || c == '&')) break;
        if (sq) {
            if (c == '\'') { sq = 0; i++; continue; }
        } else if (dq) {
            if (c == '"') { dq = 0; i++; continue; }
            if (c == '\\' && i + 1 < n) { c = s[i + 1]; i++; }
        } else {
            if (c == '\'') { sq = 1; i++; continue; }
            if (c == '"')  { dq = 1; i++; continue; }
            if (c == '\\' && i + 1 < n) { c = s[i + 1]; i++; }
        }
        if (len + 1 >= cap) { cap *= 2; w = xrealloc(w, cap); }
        w[len++] = c;
        i++;
    }
    w[len] = 0;
    *ip = i;
    if (len == 0) { xfree(w); return NULL; }
    return w;
}

static int match_at(const char *s, size_t i, size_t n, const char *lit)
{
    size_t l = strlen(lit);
    return i + l <= n && !strncmp(s + i, lit, l);
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
        char c = s[i];

        /* Кавычки копируем целиком, чтобы '>' внутри них не считался
         * редиректом. */
        if (c == '\'' || c == '"') {
            char q = c;
            buf[o++] = s[i++];
            while (i < n) {
                buf[o++] = s[i];
                if (q == '"' && s[i] == '\\' && i + 1 < n) {
                    buf[o++] = s[i + 1];
                    i += 2;
                    continue;
                }
                if (s[i] == q) { i++; break; }
                i++;
            }
            continue;
        }
        if (c == '\\' && i + 1 < n) {
            buf[o++] = s[i++];
            buf[o++] = s[i++];
            continue;
        }

        /* 2>&1 / 1>&2 — dup2 через пустое имя файла. */
        if (match_at(s, i, n, "2>&1") || match_at(s, i, n, "1>&2")) {
            int stderr_to_stdout = (s[i] == '2');
            i += 4;
            if (stderr_to_stdout) {
                if (errfile) { xfree(*errfile); *errfile = xstrdup(""); }
            } else {
                if (outfile) { xfree(*outfile); *outfile = xstrdup(""); }
                if (append) *append = 0;
            }
            changed = 1;
            continue;
        }
        if (match_at(s, i, n, "2>>") || match_at(s, i, n, "2>") ||
            match_at(s, i, n, "1>>") || match_at(s, i, n, "1>")) {
            int fd_is_err = (s[i] == '2');
            int app = (s[i + 1] == '>');
            i += app ? 3 : 2;
            char *w = read_redir_word(s, &i, n);
            if (w) {
                if (fd_is_err) {
                    if (errfile) { xfree(*errfile); *errfile = w; }
                    else xfree(w);
                    if (errappend) *errappend = app;
                } else {
                    if (outfile) { xfree(*outfile); *outfile = w; }
                    else xfree(w);
                    if (append) *append = app;
                }
                changed = 1;
                continue;
            }
        }
        if (match_at(s, i, n, "&>>") || match_at(s, i, n, "&>")) {
            int app = (s[i + 1] == '>');
            i += app ? 3 : 2;
            char *w = read_redir_word(s, &i, n);
            if (w) {
                char *dup = xstrdup(w);
                if (outfile) { xfree(*outfile); *outfile = w; }
                else xfree(w);
                if (append) *append = app;
                /* &> файл = stdout и stderr в один файл. */
                if (errfile) { xfree(*errfile); *errfile = dup; }
                else xfree(dup);
                if (errappend) *errappend = app;
                changed = 1;
                continue;
            }
        }
        if (match_at(s, i, n, ">>") || (c == '>')) {
            int app = match_at(s, i, n, ">>");
            i += app ? 2 : 1;
            char *w = read_redir_word(s, &i, n);
            if (w) {
                if (outfile) { xfree(*outfile); *outfile = w; }
                else xfree(w);
                if (append) *append = app;
                changed = 1;
                continue;
            }
        }
        if (c == '<') {
            i += 1;
            char *w = read_redir_word(s, &i, n);
            if (w) {
                if (infile) { xfree(*infile); *infile = w; }
                else xfree(w);
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
