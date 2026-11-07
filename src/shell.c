#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <time.h>

static char *build_prompt(struct yaitso_ctx *c)
{
    const char *tmpl = c->prompt ? c->prompt : YAITSO_PROMPT_DEFAULT;

    char host[128] = "yaitso";
    gethostname(host, sizeof(host) - 1);
    char *dot = strchr(host, '.');
    if (dot) *dot = 0;

    const char *user = env_get(c, "USER");
    if (!user) {
        struct passwd *pw = getpwuid(getuid());
        user = pw ? pw->pw_name : "?";
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), "?");

    const char *home = env_get(c, "HOME");
    const char *wd = cwd;
    char wds[PATH_MAX];
    if (home && !strncmp(cwd, home, strlen(home))) {
        snprintf(wds, sizeof(wds), "~%s", cwd + strlen(home));
        wd = wds;
    }
    const char *base = strrchr(wd, '/');
    if (base && base[1]) base++;
    else base = wd;

    char *out = xmalloc(strlen(tmpl) * 2 + 256);
    size_t o = 0;
    for (const char *p = tmpl; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'u': { size_t l = strlen(user); memcpy(out + o, user, l); o += l; break; }
            case 'h': { size_t l = strlen(host); memcpy(out + o, host, l); o += l; break; }
            case 'w': { size_t l = strlen(wd);   memcpy(out + o, wd,   l); o += l; break; }
            case 'W': { size_t l = strlen(base); memcpy(out + o, base, l); o += l; break; }
            case '$': out[o++] = (geteuid() == 0) ? '#' : '$'; break;
            case 'n': out[o++] = '\n'; break;
            case 'e': out[o++] = '\033'; break;
            case '\\': out[o++] = '\\'; break;
            default: out[o++] = *p; break;
            }
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
    return out;
}


int shell_run(struct yaitso_ctx *c)
{
    jobs_init();

    if (c->interactive) {
        printf("Yaitso OS %s\n", YAITSO_VERSION);
        printf("Введи 'help' для списка команд.\n\n");
    }

    for (;;) {
        jobs_reap(c);
        if (c->should_exit) break;

        char *prompt = build_prompt(c);
        char *line = readline_yaitso(c, prompt);
        xfree(prompt);
        if (!line) break;

        char *t = trim(line);
        if (*t) history_add(c, t);

        c->lineno++;

        char *expanded = NULL;
        {
            const char *p = t;
            while (*p == ' ') p++;
            const char *st = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (p > st) {
                char name[128];
                size_t nl = (size_t)(p - st);
                if (nl < sizeof(name)) {
                    memcpy(name, st, nl);
                    name[nl] = 0;
                    const char *av = alias_get(c, name);
                    if (av) {
                        size_t rl = strlen(p);
                        expanded = xmalloc(strlen(av) + rl + 2);
                        memcpy(expanded, av, strlen(av));
                        memcpy(expanded + strlen(av), p, rl + 1);
                    }
                }
            }
        }
        char *runline = expanded ? expanded : t;

        char **argv = NULL;
        int argc = 0;
        split_words(runline, &argv, &argc);

        if (argc > 0 && commands_run(c, argc, argv) == 0) {
        } else if (argc > 0) {
            exec_line(c, runline);
        }

        for (int i = 0; i < argc; i++) xfree(argv[i]);
        xfree(argv);
        xfree(expanded);
        xfree(line);
    }

    if (c->interactive) history_save(c);
    return c->exit_code;
}
