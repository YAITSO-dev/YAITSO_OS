#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *known[] = {
    "help", "version", "about", "welcome", "clear", "reset",
    "echo", "printf", "true", "false", "date", "uname", "whoami",
    "history", "sleep", "time", "set", "export", "unset", "env",
    "alias", "unalias", "which", "type", "jobs", "fg", "bg",
    "wait", "shift", "cd", "pwd", "ls", "cat", "head", "tail",
    "wc", "grep", "seq", "pushd", "popd", "dirs", "source",
    "read", "exit", "logout", "ls", "cp", "mv", "rm", "mkdir",
    "rmdir", "ln", "chmod", "chown", "ps", "kill", "find",
    "mount", "umount", "df", "du", "free", "top", "sort",
    "uniq", "sed", "awk", "hostname", "uptime", "sync",
    "reboot", "poweroff", "basename", "dirname", "yes", "test",
    "yat", "svc", "ywifi", "yaitso-control", NULL
};

static int levenshtein(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0) return (int)lb;
    if (lb == 0) return (int)la;
    if (la > 64 || lb > 64) return 99;

    int prev[65], cur[65];
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int m = prev[j] + 1;
            if (cur[j-1] + 1 < m) m = cur[j-1] + 1;
            if (prev[j-1] + cost < m) m = prev[j-1] + cost;
            cur[j] = m;
        }
        memcpy(prev, cur, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

const char *suggest_command(const char *name)
{
    if (!name || !name[0]) return NULL;
    const char *best = NULL;
    int bestd = 3;
    for (int i = 0; known[i]; i++) {
        int d = levenshtein(name, known[i]);
        if (d < bestd) { bestd = d; best = known[i]; }
    }
    return best;
}

char *suggest_fix(struct yaitso_ctx *c, const char *line)
{
    (void)c;
    if (!line) return NULL;
    const char *p = line;
    while (*p == ' ') p++;
    const char *st = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (p == st) return NULL;
    char cmd[64];
    size_t nl = (size_t)(p - st);
    if (nl >= sizeof(cmd)) return NULL;
    memcpy(cmd, st, nl);
    cmd[nl] = 0;

    const char *sug = suggest_command(cmd);
    if (!sug) return NULL;

    size_t sl = strlen(sug);
    size_t rest = strlen(p);
    char *out = xmalloc(sl + rest + 2);
    memcpy(out, sug, sl);
    out[sl] = ' ';
    memcpy(out + sl + 1, p, rest);
    out[sl + 1 + rest] = 0;
    return out;
}
