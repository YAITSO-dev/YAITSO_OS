#include "ylaunch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' ||
                     e[-1] == ' '  || e[-1] == '\t')) *--e = 0;
    return s;
}

static int parse_desktop(const char *path, struct yl_app *a)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(a, 0, sizeof(*a));
    char line[512];
    int in_entry = 0;
    int got_name = 0, got_exec = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '[') { in_entry = !strcmp(p, "[Desktop Entry]"); continue; }
        if (!in_entry || !*p || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p);
        char *v = trim(eq + 1);
        if (!strcmp(k, "Name") && !got_name) {
            snprintf(a->name, sizeof(a->name), "%s", v);
            got_name = 1;
        } else if (!strcmp(k, "Exec") && !got_exec) {
            snprintf(a->exec, sizeof(a->exec), "%s", v);
            got_exec = 1;
        } else if (!strcmp(k, "Icon")) {
            snprintf(a->icon, sizeof(a->icon), "%s", v);
        } else if (!strcmp(k, "Terminal")) {
            a->terminal = !strcmp(v, "true");
        } else if (!strcmp(k, "Type")) {
            if (strcmp(v, "Application")) { fclose(f); return -1; }
        } else if (!strcmp(k, "NoDisplay")) {
            if (!strcmp(v, "true")) { fclose(f); return -1; }
        }
    }
    fclose(f);
    return (got_name && got_exec) ? 0 : -1;
}

static int scan_dir(const char *dir, struct yl_app *apps, int n, int max)
{
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l < 8 || strcmp(de->d_name + l - 8, ".desktop")) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (parse_desktop(path, &apps[n]) == 0) n++;
    }
    closedir(d);
    return n;
}

int yl_scan(struct yl_app *apps, int max)
{
    int n = 0;
    n = scan_dir("/usr/share/applications", apps, n, max);
    n = scan_dir("/usr/local/share/applications", apps, n, max);
    n = scan_dir("/etc/yaitso/desktop.d", apps, n, max);
    return n;
}

static char *next_tok(char **p)
{
    while (**p == ' ') (*p)++;
    if (!**p) return NULL;
    char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) { **p = 0; (*p)++; }
    return start;
}

void yl_launch(const struct yl_app *a)
{
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s", a->exec);
    char clean[512];
    int ci = 0;
    for (char *p = cmd; *p && ci < (int)sizeof(clean) - 1; ) {
        if (*p == '%' && p[1]) {
            if (p[1] == '%') clean[ci++] = '%';
            p += 2;
            continue;
        }
        clean[ci++] = *p++;
    }
    clean[ci] = 0;

    char *argv[16];
    int n = 0;
    char *p = clean;
    char *tok;
    while ((tok = next_tok(&p)) && n < 15) argv[n++] = tok;
    argv[n] = NULL;

    if (a->terminal) {
        /* yterm -e ожидает отдельные argv, а не одну строку с пробелами. */
        char *targv[18];
        int tn = 0;
        targv[tn++] = "yterm";
        targv[tn++] = "-e";
        for (int i = 0; i < n && tn < 17; i++) targv[tn++] = argv[i];
        targv[tn] = NULL;
        execvp("yterm", targv);
        _exit(127);
    }

    if (n > 0) execvp(argv[0], argv);
    _exit(127);
}
