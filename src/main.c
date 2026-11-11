#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <limits.h>

static void set_posargs(struct yaitso_ctx *c, int n, char **v)
{
    for (int i = 0; i < c->posargc; i++) xfree(c->posargs[i]);
    xfree(c->posargs);
    c->posargs = NULL;
    c->posargc = 0;
    if (n <= 0) return;
    c->posargs = xmalloc(sizeof(char *) * (size_t)n);
    for (int i = 0; i < n; i++)
        c->posargs[i] = xstrdup(v[i]);
    c->posargc = n;
}

static void init_ctx(struct yaitso_ctx *c)
{
    memset(c, 0, sizeof(*c));
    c->prompt = xstrdup(YAITSO_PROMPT_DEFAULT);
    c->histsize = YAITSO_HISTORY_DEFAULT;
    c->interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    c->last_status = 0;
    c->last_bg_pid = 0;
    c->cwd_prev = NULL;
    c->should_exit = 0;
    c->exit_code = 0;
    c->lineno = 0;

    extern char **environ;
    for (char **p = environ; *p; p++) {
        char *eq = strchr(*p, '=');
        if (!eq) continue;
        *eq = 0;
        env_set(c, *p, eq + 1);
        *eq = '=';
    }

    if (!env_get(c, "HOME")) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) env_set(c, "HOME", pw->pw_dir);
    }
    if (!env_get(c, "PATH"))
        env_set(c, "PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");
    if (!env_get(c, "USER")) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) env_set(c, "USER", pw->pw_name);
    }
    if (!env_get(c, "SHELL"))
        env_set(c, "SHELL", "/usr/local/bin/yaitso");

    const char *home = env_get(c, "HOME");
    char histpath[PATH_MAX];
    if (home) {
        snprintf(histpath, sizeof(histpath), "%s/.yaitso_history", home);
        history_init(c, c->histsize, histpath);
    }

    config_load(c);
}

static void free_ctx(struct yaitso_ctx *c)
{
    xfree(c->prompt);
    xfree(c->histfile);
    xfree(c->cwd_prev);
    for (int i = 0; i < c->env_count; i++) {
        xfree(c->env[i].key);
        xfree(c->env[i].value);
    }
    xfree(c->env);
    for (int i = 0; i < c->alias_count; i++) {
        xfree(c->aliases[i].name);
        xfree(c->aliases[i].value);
    }
    xfree(c->aliases);
    for (int i = 0; i < c->posargc; i++) xfree(c->posargs[i]);
    xfree(c->posargs);
    history_free(c);
}

int main(int argc, char **argv)
{
    struct yaitso_ctx ctx;
    init_ctx(&ctx);

    if (argc >= 2 && !strcmp(argv[1], "-c")) {
        int i = 2;
        if (i < argc && !strcmp(argv[i], "--")) i++;
        if (i >= argc) { free_ctx(&ctx); return 0; }
        /* argv[i] — команда; argv[i+1] — $0, дальше $1... */
        if (i + 1 < argc) set_posargs(&ctx, argc - (i + 1), argv + i + 1);
        else { char *z = "yaitso"; set_posargs(&ctx, 1, &z); }
        int rc = exec_line(&ctx, argv[i]);
        free_ctx(&ctx);
        return rc;
    }

    if (argc >= 2 && argv[1][0] != '-') {
        set_posargs(&ctx, argc - 1, argv + 1);
        FILE *f = fopen(argv[1], "r");
        if (!f) { perror(argv[1]); free_ctx(&ctx); return 1; }
        char line[8192];
        int rc = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            char *t = trim(line);
            if (!*t || *t == '#') continue;
            rc = exec_line(&ctx, t);
        }
        fclose(f);
        free_ctx(&ctx);
        return rc;
    }

    if (argc > 0) set_posargs(&ctx, 1, argv);
    int rc = shell_run(&ctx);
    free_ctx(&ctx);
    return rc;
}

int yaitso_main(int argc, char **argv)
{
    return main(argc, argv);
}
