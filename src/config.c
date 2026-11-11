#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static void strip_quotes(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n-1] == '"') ||
                   (s[0] == '\'' && s[n-1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n-2] = 0;
    }
}

int config_load_file(struct yaitso_ctx *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;

        if (!strncmp(p, "set ", 4)) {
            char *kv = trim(p + 4);
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq = 0;
                char *k = trim(kv);
                char *v = trim(eq + 1);
                strip_quotes(v);
                if (!strcmp(k, "PROMPT")) {
                    xfree(c->prompt);
                    c->prompt = xstrdup(v);
                } else if (!strcmp(k, "HISTSIZE")) {
                    c->histsize = atoi(v);
                } else if (!strcmp(k, "HISTFILE")) {
                    xfree(c->histfile);
                    c->histfile = xstrdup(v);
                } else if (!strcmp(k, "EDITOR")) {
                    env_set(c, "EDITOR", v);
                } else {
                    env_set(c, k, v);
                }
            }
            continue;
        }

        if (!strncmp(p, "export ", 7)) {
            char *kv = trim(p + 7);
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq = 0;
                char *k = trim(kv);
                char *v = trim(eq + 1);
                strip_quotes(v);
                env_set(c, k, v);
            }
            continue;
        }

        if (!strncmp(p, "alias ", 6)) {
            char *kv = trim(p + 6);
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq = 0;
                char *k = trim(kv);
                char *v = trim(eq + 1);
                strip_quotes(v);
                alias_set(c, k, v);
            }
            continue;
        }
    }
    fclose(f);
    return 0;
}

int config_load_dir(struct yaitso_ctx *c, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l < 4 || strcmp(de->d_name + l - 3, ".rc")) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        config_load_file(c, path);
    }
    closedir(d);
    return 0;
}

int config_load(struct yaitso_ctx *c)
{
    const char *home = env_get(c, "HOME");
    if (!home) home = getenv("HOME");
    if (!home) home = "/root";

    char path[1024];
    snprintf(path, sizeof(path), "%s/.yaitso_rc", home);
    config_load_file(c, path);

    snprintf(path, sizeof(path), "%s/.yaitso.d", home);
    config_load_dir(c, path);

    return 0;
}
