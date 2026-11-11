#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <sys/wait.h>

int builtin_help(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    print_help();
    return 0;
}

int builtin_version(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    print_version();
    return 0;
}

int builtin_about(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    printf("Yaitso OS %s\n", YAITSO_VERSION);
    printf("Yet Another Independent Typical Stable Origin OS\n");
    printf("Независимый Linux-дистрибутив с собственным шеллом,\n");
    printf("coreutils, пакетным менеджером и Wayland-композитором.\n");
    return 0;
}

int builtin_welcome(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    printf("\n");
    printf("  Добро пожаловать в Yaitso OS %s\n", YAITSO_VERSION);
    printf("  help     — список команд\n");
    printf("  yat      — пакетный менеджер\n");
    printf("  svc      — сервисы\n");
    printf("  yaitso-control — панель управления (TUI)\n");
    printf("\n");
    return 0;
}

int builtin_clear(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

int builtin_reset(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    printf("\033c");
    fflush(stdout);
    return 0;
}

int builtin_echo(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int nl = 1;
    int esc = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "-n")) { nl = 0; i++; continue; }
        if (!strcmp(argv[i], "-e")) { esc = 1; i++; continue; }
        if (!strcmp(argv[i], "-E")) { esc = 0; i++; continue; }
        break;
    }
    for (int k = i; k < argc; k++) {
        if (k > i) putchar(' ');
        if (!esc) { fputs(argv[k], stdout); continue; }
        for (const char *p = argv[k]; *p; p++) {
            if (*p != '\\') { putchar(*p); continue; }
            p++;
            switch (*p) {
            case 'n': putchar('\n'); break;
            case 't': putchar('\t'); break;
            case 'r': putchar('\r'); break;
            case '\\': putchar('\\'); break;
            case 'a': putchar('\a'); break;
            case 'b': putchar('\b'); break;
            case 'f': putchar('\f'); break;
            case 'v': putchar('\v'); break;
            case '0': {
                int v = 0, cnt = 0;
                while (cnt < 3 && p[1] >= '0' && p[1] <= '7') {
                    p++; v = v * 8 + (*p - '0'); cnt++;
                }
                putchar(v);
                break;
            }
            case 0: p--; break;
            default: putchar('\\'); putchar(*p); break;
            }
        }
    }
    if (nl) putchar('\n');
    fflush(stdout);
    return 0;
}

/* Выводит один backslash-escape (p указывает на символ после '\\'). */
static const char *printf_escape(const char *p)
{
    switch (*p) {
    case 'n': putchar('\n'); return p;
    case 't': putchar('\t'); return p;
    case 'r': putchar('\r'); return p;
    case '\\': putchar('\\'); return p;
    case 'a': putchar('\a'); return p;
    case 'b': putchar('\b'); return p;
    case 'f': putchar('\f'); return p;
    case 'v': putchar('\v'); return p;
    case '0': {
        int v = 0, cnt = 0;
        while (cnt < 3 && p[1] >= '0' && p[1] <= '7') {
            p++; v = v * 8 + (*p - '0'); cnt++;
        }
        putchar(v);
        return p;
    }
    case 0: return p - 1;
    default: putchar('\\'); putchar(*p); return p;
    }
}

int builtin_printf(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    if (argc < 2) { fprintf(stderr, "printf: нужен формат\n"); return 1; }
    const char *fmt = argv[1];
    int argi = 2;

    /* POSIX: формат повторяется, пока есть аргументы. */
    int again = 1;
    while (again) {
        int consumed = 0;
        for (const char *p = fmt; *p; p++) {
            if (*p == '\\') { p = printf_escape(p + 1); continue; }
            if (*p != '%') { putchar(*p); continue; }
            p++;
            if (*p == '%') { putchar('%'); continue; }
            int width = 0;
            int zero = 0;
            if (*p == '0') { zero = 1; p++; }
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
            char spec = *p;
            const char *arg = (argi < argc) ? argv[argi++] : "";
            consumed = 1;
            switch (spec) {
            case 's': printf("%*s", width, arg); break;
            case 'd': case 'i': {
                long v = strtol(arg, NULL, 10);
                if (zero) printf("%0*ld", width, v);
                else printf("%*ld", width, v);
                break;
            }
            case 'u': {
                unsigned long v = strtoul(arg, NULL, 10);
                printf("%*lu", width, v);
                break;
            }
            case 'x': {
                unsigned long v = strtoul(arg, NULL, 16);
                printf("%*lx", width, v);
                break;
            }
            case 'X': {
                unsigned long v = strtoul(arg, NULL, 16);
                printf("%*lX", width, v);
                break;
            }
            case 'o': {
                unsigned long v = strtoul(arg, NULL, 8);
                printf("%*lo", width, v);
                break;
            }
            case 'c': if (arg[0]) putchar(arg[0]); break;
            case 0: p--; break;
            default: putchar('%'); putchar(spec); break;
            }
        }
        again = (consumed && argi < argc);
    }
    fflush(stdout);
    return 0;
}

int builtin_true(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    return 0;
}

int builtin_false(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    return 1;
}

int builtin_date(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int utc = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--utc")) utc = 1;
    time_t t = time(NULL);
    struct tm tm;
    if (utc) gmtime_r(&t, &tm);
    else     localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y", &tm);
    printf("%s\n", buf);
    return 0;
}

int builtin_uname(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    struct utsname u;
    if (uname(&u) != 0) { perror("uname"); return 1; }
    int all = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-a")) all = 1;
    if (all) {
        printf("%s %s %s %s %s\n", u.sysname, u.nodename, u.release,
               u.version, u.machine);
    } else {
        int printed = 0;
        for (int i = 1; i < argc; i++) {
            if (printed) putchar(' ');
            if (!strcmp(argv[i], "-s")) fputs(u.sysname, stdout);
            else if (!strcmp(argv[i], "-n")) fputs(u.nodename, stdout);
            else if (!strcmp(argv[i], "-r")) fputs(u.release, stdout);
            else if (!strcmp(argv[i], "-v")) fputs(u.version, stdout);
            else if (!strcmp(argv[i], "-m")) fputs(u.machine, stdout);
            else fputs(u.sysname, stdout);
            printed = 1;
        }
        if (!printed) fputs(u.sysname, stdout);
        putchar('\n');
    }
    return 0;
}

int builtin_whoami(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *u = env_get(c, "USER");
    if (!u) {
        struct passwd *pw = getpwuid(getuid());
        u = pw ? pw->pw_name : "?";
    }
    printf("%s\n", u);
    return 0;
}

int builtin_sleep(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    if (argc < 2) { fprintf(stderr, "sleep: нужен аргумент\n"); return 1; }
    double s = strtod(argv[1], NULL);
    if (s <= 0) return 0;
    struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
    nanosleep(&ts, NULL);
    return 0;
}

int builtin_time(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    if (argc < 2) { fprintf(stderr, "time: нужна команда\n"); return 1; }
    time_t start = time(NULL);
    char cmd[1024] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
    }
    int rc = exec_line(c, cmd);
    time_t end = time(NULL);
    fprintf(stderr, "\nreal\t%lds\n", (long)(end - start));
    return rc;
}

int builtin_set(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc == 1) {
        for (int i = 0; i < c->env_count; i++)
            printf("%s=%s\n", c->env[i].key, c->env[i].value);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = 0;
            env_set(c, argv[i], eq + 1);
            *eq = '=';
        }
    }
    return 0;
}

int builtin_export(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc == 1) {
        for (int i = 0; i < c->env_count; i++)
            printf("export %s=%s\n", c->env[i].key, c->env[i].value);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = 0;
            env_set(c, argv[i], eq + 1);
            *eq = '=';
        } else {
            const char *v = getenv(argv[i]);
            if (v) env_set(c, argv[i], v);
        }
    }
    return 0;
}

int builtin_unset(struct yaitso_ctx *c, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) env_unset(c, argv[i]);
    return 0;
}

int builtin_env(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < c->env_count; i++)
        printf("%s=%s\n", c->env[i].key, c->env[i].value);
    return 0;
}

int builtin_alias(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc == 1) {
        for (int i = 0; i < c->alias_count; i++)
            printf("alias %s='%s'\n", c->aliases[i].name, c->aliases[i].value);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = 0;
            char *v = eq + 1;
            if (v[0] == '\'' || v[0] == '"') {
                size_t vl = strlen(v);
                if (vl >= 2 && v[vl-1] == v[0]) { v[vl-1] = 0; v++; }
            }
            alias_set(c, argv[i], v);
            *eq = '=';
        } else {
            const char *v = alias_get(c, argv[i]);
            if (v) printf("alias %s='%s'\n", argv[i], v);
        }
    }
    return 0;
}

int builtin_unalias(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "-a")) {
        while (c->alias_count > 0) {
            c->alias_count--;
            xfree(c->aliases[c->alias_count].name);
            xfree(c->aliases[c->alias_count].value);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) alias_unset(c, argv[i]);
    return 0;
}

int builtin_which(struct yaitso_ctx *c, int argc, char **argv)
{
    int all = 0;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-a")) { all = 1; i++; }
    int rc = 0;
    for (; i < argc; i++) {
        if (alias_get(c, argv[i])) {
            printf("%s: alias\n", argv[i]);
            continue;
        }
        const char *path = getenv("PATH");
        if (!path) path = "/usr/local/bin:/usr/bin:/bin";
        char *copy = xstrdup(path);
        char *save = NULL;
        char *tok = strtok_r(copy, ":", &save);
        int found = 0;
        while (tok) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", tok, argv[i]);
            if (access(full, X_OK) == 0) {
                printf("%s\n", full);
                found = 1;
                if (!all) break;
            }
            tok = strtok_r(NULL, ":", &save);
        }
        xfree(copy);
        if (!found) rc = 1;
    }
    return rc;
}

int builtin_type(struct yaitso_ctx *c, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (alias_get(c, argv[i])) {
            printf("%s is aliased to `%s'\n", argv[i], alias_get(c, argv[i]));
        } else if (ext_command_exists(argv[i])) {
            printf("%s is a program\n", argv[i]);
        } else {
            printf("%s: not found\n", argv[i]);
        }
    }
    return 0;
}

int builtin_jobs(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    jobs_reap(c);
    jobs_print(c);
    return 0;
}

int builtin_fg(struct yaitso_ctx *c, int argc, char **argv)
{
    int id = argc > 1 ? atoi(argv[1]) : -1;
    return jobs_fg(c, id);
}

int builtin_bg(struct yaitso_ctx *c, int argc, char **argv)
{
    int id = argc > 1 ? atoi(argv[1]) : -1;
    return jobs_bg(c, id);
}

int builtin_wait(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    return jobs_wait_all(c);
}

int builtin_shift(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    return 0;
}

int builtin_cd(struct yaitso_ctx *c, int argc, char **argv)
{
    const char *target;
    if (argc < 2) target = env_get(c, "HOME");
    else if (!strcmp(argv[1], "-")) target = c->cwd_prev;
    else target = argv[1];
    if (!target) { fprintf(stderr, "cd: нет HOME\n"); return 1; }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    xfree(c->cwd_prev);
    c->cwd_prev = xstrdup(cwd);
    char *ncwd = getcwd(NULL, 0);
    if (ncwd) { env_set(c, "PWD", ncwd); free(ncwd); }
    return 0;
}

int builtin_pwd(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) { perror("pwd"); return 1; }
    printf("%s\n", cwd);
    return 0;
}

int builtin_ls(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int long_fmt = 0, one = 0, show_all = 0, human = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'l') long_fmt = 1;
            else if (*p == '1') one = 1;
            else if (*p == 'a') show_all = 1;
            else if (*p == 'h') human = 1;
        }
    }
    const char *dir = (i < argc) ? argv[i] : ".";
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "ls: %s: %s\n", dir, strerror(errno)); return 1; }

    char **names = xmalloc(sizeof(char *) * 32);
    int n = 0, cap = 32;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!show_all && de->d_name[0] == '.') continue;
        if (n >= cap) { cap *= 2; names = xrealloc(names, sizeof(char *) * (size_t)cap); }
        names[n++] = xstrdup(de->d_name);
    }
    closedir(d);

    for (int a = 0; a < n - 1; a++)
        for (int b = a + 1; b < n; b++)
            if (strcmp(names[a], names[b]) > 0) {
                char *t = names[a]; names[a] = names[b]; names[b] = t;
            }

    if (long_fmt) {
        for (int k = 0; k < n; k++) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, names[k]);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            char perms[11] = "----------";
            if (S_ISDIR(st.st_mode)) perms[0] = 'd';
            else if (S_ISLNK(st.st_mode)) perms[0] = 'l';
            if (st.st_mode & S_IRUSR) perms[1] = 'r';
            if (st.st_mode & S_IWUSR) perms[2] = 'w';
            if (st.st_mode & S_IXUSR) perms[3] = 'x';
            if (st.st_mode & S_IRGRP) perms[4] = 'r';
            if (st.st_mode & S_IWGRP) perms[5] = 'w';
            if (st.st_mode & S_IXGRP) perms[6] = 'x';
            if (st.st_mode & S_IROTH) perms[7] = 'r';
            if (st.st_mode & S_IWOTH) perms[8] = 'w';
            if (st.st_mode & S_IXOTH) perms[9] = 'x';
            if (human) {
                double sz = (double)st.st_size;
                const char *unit = "B";
                if (sz > 1024) { sz /= 1024; unit = "K"; }
                if (sz > 1024) { sz /= 1024; unit = "M"; }
                if (sz > 1024) { sz /= 1024; unit = "G"; }
                printf("%s %3.1f%s %s\n", perms, sz, unit, names[k]);
            } else {
                printf("%s %8ld %s\n", perms, (long)st.st_size, names[k]);
            }
        }
    } else if (one) {
        for (int k = 0; k < n; k++) printf("%s\n", names[k]);
    } else {
        int col = 0;
        for (int k = 0; k < n; k++) {
            printf("%-20s", names[k]);
            if (++col % 4 == 0) putchar('\n');
        }
        if (col % 4 != 0) putchar('\n');
    }

    for (int k = 0; k < n; k++) xfree(names[k]);
    xfree(names);
    return 0;
}

int builtin_cat(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int number = 0;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-n")) { number = 1; i++; }
    int lineno = 0;
    if (i >= argc) {
        char buf[4096];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), stdin)) > 0)
            fwrite(buf, 1, r, stdout);
        return 0;
    }
    for (; i < argc; i++) {
        FILE *f;
        if (!strcmp(argv[i], "-")) f = stdin;
        else {
            f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno)); continue; }
        }
        char buf[4096];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
            if (number) {
                for (size_t j = 0; j < r; j++) {
                    if (buf[j] == '\n' || (j == 0 && lineno == 0)) {
                        lineno++;
                        printf("%6d\t", lineno);
                    }
                    putchar(buf[j]);
                }
            } else {
                fwrite(buf, 1, r, stdout);
            }
        }
        if (f != stdin) fclose(f);
    }
    return 0;
}

int builtin_head(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int n = 10;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-n") && i + 1 < argc) {
        n = atoi(argv[i + 1]);
        i += 2;
    }
    FILE *f = stdin;
    if (i < argc && strcmp(argv[i], "-")) {
        f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); return 1; }
    }
    char line[8192];
    int k = 0;
    while (k < n && fgets(line, sizeof(line), f)) {
        fputs(line, stdout);
        k++;
    }
    if (f != stdin) fclose(f);
    return 0;
}

int builtin_tail(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int n = 10;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-n") && i + 1 < argc) {
        n = atoi(argv[i + 1]);
        i += 2;
    }
    if (n <= 0) {
        /* tail -n 0 (и отрицательные значения) — пустой вывод. */
        return 0;
    }
    FILE *f = stdin;
    if (i < argc && strcmp(argv[i], "-")) {
        f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); return 1; }
    }
    char **ring = xmalloc(sizeof(char *) * (size_t)n);
    for (int k = 0; k < n; k++) ring[k] = NULL;
    int cnt = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        int slot = cnt % n;
        xfree(ring[slot]);
        ring[slot] = xstrdup(line);
        cnt++;
    }
    int start = cnt >= n ? cnt % n : 0;
    int total = cnt < n ? cnt : n;
    for (int k = 0; k < total; k++) {
        int slot = (start + k) % n;
        if (ring[slot]) fputs(ring[slot], stdout);
    }
    for (int k = 0; k < n; k++) xfree(ring[k]);
    xfree(ring);
    if (f != stdin) fclose(f);
    return 0;
}

int builtin_wc(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int want_w = 0, want_l = 0, want_c = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'w') want_w = 1;
            else if (*p == 'l') want_l = 1;
            else if (*p == 'c') want_c = 1;
        }
    }
    if (!want_w && !want_l && !want_c) { want_l = want_w = want_c = 1; }
    FILE *f = stdin;
    if (i < argc && strcmp(argv[i], "-")) {
        f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); return 1; }
    }
    long lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        bytes++;
        if (ch == '\n') lines++;
        if (isspace(ch)) in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
    }
    int nsel = want_l + want_w + want_c;
    /* Как GNU wc: одно поле — без выравнивания, несколько — шириной 7. */
    if (nsel == 1) {
        if (want_l) printf("%ld", lines);
        else if (want_w) printf("%ld", words);
        else printf("%ld", bytes);
    } else {
        if (want_l) printf("%7ld", lines);
        if (want_w) printf("%7ld", words);
        if (want_c) printf("%7ld", bytes);
    }
    putchar('\n');
    if (f != stdin) fclose(f);
    return 0;
}

int builtin_grep(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int icase = 0, invert = 0, number = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'i') icase = 1;
            else if (*p == 'v') invert = 1;
            else if (*p == 'n') number = 1;
        }
    }
    if (i >= argc) { fprintf(stderr, "grep: нужен образец\n"); return 1; }
    const char *pat = argv[i++];
    FILE *f = stdin;
    if (i < argc && strcmp(argv[i], "-")) {
        f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); return 1; }
    }
    char *lpat = xstrdup(pat);
    if (icase)
        for (char *q = lpat; *q; q++) *q = (char)tolower((unsigned char)*q);
    char line[8192];
    int lineno = 0, found = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *cmp = line;
        char *temp = NULL;
        if (icase) {
            temp = xstrdup(line);
            for (char *q = temp; *q; q++) *q = (char)tolower((unsigned char)*q);
            cmp = temp;
        }
        int hit = strstr(cmp, lpat) != NULL;
        xfree(temp);
        if (hit ^ invert) {
            if (number) printf("%d:", lineno);
            fputs(line, stdout);
            found = 1;
        }
    }
    xfree(lpat);
    if (f != stdin) fclose(f);
    return found ? 0 : 1;
}

int builtin_seq(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    long first = 1, step = 1, last = 1;
    if (argc == 2) last = atol(argv[1]);
    else if (argc == 3) { first = atol(argv[1]); last = atol(argv[2]); }
    else if (argc == 4) { first = atol(argv[1]); step = atol(argv[2]); last = atol(argv[3]); }
    else { fprintf(stderr, "seq: неверные аргументы\n"); return 1; }
    if (step == 0) return 1;
    if (step > 0) {
        for (long i = first; i <= last; i += step) printf("%ld\n", i);
    } else {
        for (long i = first; i >= last; i += step) printf("%ld\n", i);
    }
    return 0;
}

int builtin_history(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "-c")) {
        history_clear(c);
        return 0;
    }
    int total = history_count();
    for (int i = 0; i < total; i++) {
        const char *l = history_get(c, i);
        if (l) printf("%5d  %s\n", i + 1, l);
    }
    return 0;
}

static char *g_dirstack[32];
static int   g_dirstack_n = 0;

int builtin_pushd(struct yaitso_ctx *c, int argc, char **argv)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 1;
    if (argc >= 2) {
        if (chdir(argv[1]) != 0) { perror("pushd"); return 1; }
    }
    if (g_dirstack_n < 32) g_dirstack[g_dirstack_n++] = xstrdup(cwd);
    return builtin_dirs(c, 0, NULL);
}

int builtin_popd(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    if (g_dirstack_n == 0) { fprintf(stderr, "popd: стек пуст\n"); return 1; }
    char *d = g_dirstack[--g_dirstack_n];
    if (chdir(d) != 0) { perror("popd"); xfree(d); return 1; }
    xfree(d);
    return builtin_dirs(c, 0, NULL);
}

int builtin_dirs(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c; (void)argc; (void)argv;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) printf("%s ", cwd);
    for (int i = g_dirstack_n - 1; i >= 0; i--) printf("%s ", g_dirstack[i]);
    putchar('\n');
    return 0;
}

int builtin_source(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "source: нужен файл\n"); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    char line[8192];
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *t = trim(line);
        if (!*t || *t == '#') continue;
        rc = exec_line(c, t);
    }
    fclose(f);
    return rc;
}

int builtin_read(struct yaitso_ctx *c, int argc, char **argv)
{
    (void)c;
    int i = 1;
    const char *prompt = NULL;
    int silent = 0;
    if (i < argc && !strcmp(argv[i], "-p") && i + 1 < argc) {
        prompt = argv[i + 1];
        i += 2;
    }
    if (i < argc && !strcmp(argv[i], "-s")) { silent = 1; i++; }
    if (i >= argc) { fprintf(stderr, "read: нужна переменная\n"); return 1; }
    if (prompt) { fputs(prompt, stdout); fflush(stdout); }

    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return 1;
    buf[strcspn(buf, "\r\n")] = 0;
    if (silent) putchar('\n');
    env_set(c, argv[i], buf);
    return 0;
}

int builtin_exit(struct yaitso_ctx *c, int argc, char **argv)
{
    int code = c->last_status;
    if (argc >= 2) code = atoi(argv[1]);
    c->exit_code = code;
    c->should_exit = 1;
    return code;
}

int builtin_logout(struct yaitso_ctx *c, int argc, char **argv)
{
    return builtin_exit(c, argc, argv);
}
