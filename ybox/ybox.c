#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <utime.h>
#include <sys/reboot.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

static int cmd_true(int argc, char **argv)  { (void)argc; (void)argv; return 0; }
static int cmd_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

static int cmd_yes(int argc, char **argv)
{
    const char *s = (argc > 1) ? argv[1] : "y";
    for (;;) {
        fputs(s, stdout);
        putchar('\n');
    }
    return 0;
}

static int cmd_basename(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "basename: missing arg\n"); return 1; }
    const char *p = strrchr(argv[1], '/');
    printf("%s\n", p ? p + 1 : argv[1]);
    return 0;
}

static int cmd_dirname(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "dirname: missing arg\n"); return 1; }
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", argv[1]);
    char *slash = strrchr(buf, '/');
    if (!slash) { printf(".\n"); return 0; }
    if (slash == buf) { printf("/\n"); return 0; }
    *slash = 0;
    printf("%s\n", buf);
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    int parents = 0;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-p")) { parents = 1; i++; }
    if (i >= argc) { fprintf(stderr, "mkdir: missing operand\n"); return 1; }
    for (; i < argc; i++) {
        if (parents) {
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof(tmp), "%s", argv[i]);
            for (char *q = tmp + 1; *q; q++) {
                if (*q == '/') { *q = 0; mkdir(tmp, 0755); *q = '/'; }
            }
            mkdir(tmp, 0755);
        } else {
            if (mkdir(argv[i], 0755) != 0) {
                fprintf(stderr, "mkdir: %s: %s\n", argv[i], strerror(errno));
                return 1;
            }
        }
    }
    return 0;
}

static int cmd_rmdir(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "rmdir: missing operand\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) != 0) {
            fprintf(stderr, "rmdir: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}

static int do_rm(const char *path, int recursive, int force)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (!force) fprintf(stderr, "rm: %s: %s\n", path, strerror(errno));
        return force ? 0 : 1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            fprintf(stderr, "rm: %s: is a directory\n", path);
            return 1;
        }
        DIR *d = opendir(path);
        if (!d) { if (!force) perror(path); return 1; }
        struct dirent *de;
        int rc = 0;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            if (do_rm(full, 1, force) != 0) rc = 1;
        }
        closedir(d);
        if (rmdir(path) != 0) { if (!force) perror(path); return 1; }
        return rc;
    }
    if (unlink(path) != 0) {
        if (!force) perror(path);
        return 1;
    }
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    int recursive = 0, force = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'r' || *p == 'R') recursive = 1;
            else if (*p == 'f') force = 1;
        }
    }
    if (i >= argc) { fprintf(stderr, "rm: missing operand\n"); return 1; }
    int rc = 0;
    for (; i < argc; i++)
        if (do_rm(argv[i], recursive, force) != 0) rc = 1;
    return rc;
}

static int copy_file(const char *src, const char *dst, mode_t mode)
{
    int in = open(src, O_RDONLY);
    if (in < 0) { perror(src); return 1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (out < 0) { perror(dst); close(in); return 1; }
    char buf[65536];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w <= 0) { perror(dst); rc = 1; goto done; }
            off += w;
        }
    }
    if (r < 0) { perror(src); rc = 1; }
done:
    if (close(out) != 0 && rc == 0) { perror(dst); rc = 1; }
    close(in);
    return rc;
}

static int copy_path(const char *src, const char *dst, int recursive)
{
    struct stat st;
    if (lstat(src, &st) != 0) { perror(src); return 1; }

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t n = readlink(src, target, sizeof(target) - 1);
        if (n < 0) { perror(src); return 1; }
        target[n] = 0;
        unlink(dst);
        if (symlink(target, dst) != 0) { perror(dst); return 1; }
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            fprintf(stderr, "cp: %s: is a directory\n", src);
            return 1;
        }
        if (mkdir(dst, st.st_mode & 07777) != 0 && errno != EEXIST) {
            perror(dst);
            return 1;
        }
        DIR *d = opendir(src);
        if (!d) { perror(src); return 1; }
        struct dirent *de;
        int rc = 0;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s2[PATH_MAX], d2[PATH_MAX];
            snprintf(s2, sizeof(s2), "%s/%s", src, de->d_name);
            snprintf(d2, sizeof(d2), "%s/%s", dst, de->d_name);
            if (copy_path(s2, d2, recursive) != 0) rc = 1;
        }
        closedir(d);
        return rc;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "cp: %s: unsupported file type\n", src);
        return 1;
    }
    return copy_file(src, dst, st.st_mode);
}

static int cmd_cp(int argc, char **argv)
{
    int recursive = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strchr(argv[i], 'r') || strchr(argv[i], 'R')) recursive = 1;
    }
    if (argc - i < 2) { fprintf(stderr, "cp: missing operand\n"); return 1; }

    const char *src = argv[i];
    const char *dst = argv[i + 1];

    struct stat sst, dstst;
    if (stat(src, &sst) != 0) { perror(src); return 1; }
    if (S_ISDIR(sst.st_mode) && !recursive) {
        fprintf(stderr, "cp: %s: is a directory\n", src);
        return 1;
    }

    /* Если dst — существующий каталог, копируем внутрь него. */
    char dest[PATH_MAX];
    if (stat(dst, &dstst) == 0 && S_ISDIR(dstst.st_mode)) {
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        snprintf(dest, sizeof(dest), "%s/%s", dst, base);
    } else {
        snprintf(dest, sizeof(dest), "%s", dst);
    }
    return copy_path(src, dest, recursive);
}

static int cmd_mv(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "mv: missing operand\n"); return 1; }
    if (rename(argv[1], argv[2]) != 0) { perror("mv"); return 1; }
    return 0;
}

static int cmd_ln(int argc, char **argv)
{
    int sym = 0;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-s")) { sym = 1; i++; }
    if (argc - i < 2) { fprintf(stderr, "ln: missing operand\n"); return 1; }
    int r = sym ? symlink(argv[i], argv[i+1]) : link(argv[i], argv[i+1]);
    if (r != 0) { perror("ln"); return 1; }
    return 0;
}

static int cmd_chmod(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "chmod: usage: chmod MODE FILE...\n"); return 1; }
    mode_t mode = (mode_t)strtol(argv[1], NULL, 8);
    int rc = 0;
    for (int i = 2; i < argc; i++)
        if (chmod(argv[i], mode) != 0) { perror(argv[i]); rc = 1; }
    return rc;
}

static int parse_uid(const char *s, uid_t *out)
{
    if (!s || !*s) return 0;
    if (*s >= '0' && *s <= '9') { *out = (uid_t)strtoul(s, NULL, 10); return 1; }
    struct passwd *pw = getpwnam(s);
    if (!pw) return 0;
    *out = pw->pw_uid;
    return 1;
}

static int parse_gid(const char *s, gid_t *out)
{
    if (!s || !*s) return 0;
    if (*s >= '0' && *s <= '9') { *out = (gid_t)strtoul(s, NULL, 10); return 1; }
    struct group *gr = getgrnam(s);
    if (!gr) return 0;
    *out = gr->gr_gid;
    return 1;
}

static int cmd_chown(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "chown: usage: chown USER[:GROUP] FILE...\n"); return 1; }
    char spec[256];
    snprintf(spec, sizeof(spec), "%s", argv[1]);
    char *colon = strchr(spec, ':');
    char *user = spec;
    char *group = NULL;
    if (colon) { *colon = 0; group = colon + 1; }

    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    if (user[0] && !parse_uid(user, &uid)) {
        fprintf(stderr, "chown: unknown user: %s\n", user);
        return 1;
    }
    if (group && *group && !parse_gid(group, &gid)) {
        fprintf(stderr, "chown: unknown group: %s\n", group);
        return 1;
    }

    int rc = 0;
    for (int i = 2; i < argc; i++)
        if (chown(argv[i], uid, gid) != 0) { perror(argv[i]); rc = 1; }
    return rc;
}

static int cmd_sync(int argc, char **argv)
{
    (void)argc; (void)argv;
    sync();
    return 0;
}

static int cmd_hostname(int argc, char **argv)
{
    if (argc < 2) {
        char h[256];
        if (gethostname(h, sizeof(h)) != 0) return 1;
        printf("%s\n", h);
        return 0;
    }
    if (sethostname(argv[1], strlen(argv[1])) != 0) { perror("sethostname"); return 1; }
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc; (void)argv;
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 1;
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) { fclose(f); return 1; }
    fclose(f);
    long s = (long)up;
    printf("up %ld days, %02ld:%02ld:%02ld\n",
           s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    sync();
    if (reboot(RB_AUTOBOOT) != 0) { perror("reboot"); return 1; }
    return 0;
}

static int cmd_poweroff(int argc, char **argv)
{
    (void)argc; (void)argv;
    sync();
    if (reboot(RB_POWER_OFF) != 0) { perror("poweroff"); return 1; }
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    int i = 1, no_create = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (const char *p = argv[i] + 1; *p; p++)
            if (*p == 'c') no_create = 1;
    }
    if (i >= argc) { fprintf(stderr, "touch: missing operand\n"); return 1; }

    int rc = 0;
    struct timespec ts[2];
    clock_gettime(CLOCK_REALTIME, &ts[0]);
    ts[1] = ts[0];

    for (; i < argc; i++) {
        struct stat st;
        if (no_create && stat(argv[i], &st) != 0) continue;
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { perror(argv[i]); rc = 1; continue; }
        close(fd);
        if (utimensat(AT_FDCWD, argv[i], ts, 0) != 0) {
            perror(argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_df(int argc, char **argv)
{
    printf("%-22s %10s %10s %10s %5s %s\n",
           "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
    int first = (argc > 1) ? 1 : 0;
    int last = (argc > 1) ? argc - 1 : 0;
    for (int i = first; i <= last; i++) {
        const char *p = (argc > 1) ? argv[i] : "/";
        struct statvfs v;
        if (statvfs(p, &v) != 0) { perror(p); continue; }
        unsigned long long bs = v.f_frsize ? v.f_frsize : v.f_bsize;
        unsigned long long total = (unsigned long long)v.f_blocks * bs / 1024;
        unsigned long long freeb = (unsigned long long)v.f_bfree * bs / 1024;
        unsigned long long avail = (unsigned long long)v.f_bavail * bs / 1024;
        unsigned long long used = total > freeb ? total - freeb : 0;
        int pct = total ? (int)(used * 100 / total) : 0;
        printf("%-22s %10llu %10llu %10llu %4d%% %s\n",
               "yaitso", total, used, avail, pct, p);
    }
    return 0;
}

static int cmd_free(int argc, char **argv)
{
    (void)argc; (void)argv;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { perror("/proc/meminfo"); return 1; }

    unsigned long long mt = 0, mf = 0, ma = 0, mb = 0, mc = 0;
    unsigned long long st = 0, sf = 0;
    char key[64];
    unsigned long long kb;
    while (fscanf(f, "%63s %llu", key, &kb) == 2) {
        if      (!strcmp(key, "MemTotal:"))     mt = kb;
        else if (!strcmp(key, "MemFree:"))      mf = kb;
        else if (!strcmp(key, "MemAvailable:")) ma = kb;
        else if (!strcmp(key, "Buffers:"))      mb = kb;
        else if (!strcmp(key, "Cached:"))       mc = kb;
        else if (!strcmp(key, "SwapTotal:"))    st = kb;
        else if (!strcmp(key, "SwapFree:"))     sf = kb;
        /* пропускаем остаток строки */
        int ch;
        while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
    }
    fclose(f);

    unsigned long long cache = mb + mc;
    unsigned long long mused = mt > mf + cache ? mt - mf - cache : mt - mf;
    unsigned long long sused = st > sf ? st - sf : 0;

    printf("%-7s %10s %10s %10s %10s %10s %10s\n",
           "", "total", "used", "free", "shared", "buff/cache", "available");
    printf("%-7s %10llu %10llu %10llu %10s %10llu %10llu\n", "Mem:",
           mt / 1024, mused / 1024, mf / 1024, "-", cache / 1024, ma / 1024);
    printf("%-7s %10llu %10llu %10llu\n", "Swap:",
           st / 1024, sused / 1024, sf / 1024);
    return 0;
}

static int cmd_mktemp(int argc, char **argv)
{
    int dir = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (strchr(argv[i], 'd')) dir = 1;
        if (strchr(argv[i], 'q')) continue;
        if (strchr(argv[i], 'u')) {
            fprintf(stderr, "mktemp: -u не поддерживается (небезопасно)\n");
            return 1;
        }
    }

    char tmpl[PATH_MAX];
    if (i < argc) snprintf(tmpl, sizeof(tmpl), "%s", argv[i]);
    else           snprintf(tmpl, sizeof(tmpl), "/tmp/tmp.XXXXXX");

    size_t l = strlen(tmpl);
    if (l < 6 || strcmp(tmpl + l - 6, "XXXXXX")) {
        if (l + 8 >= sizeof(tmpl)) {
            fprintf(stderr, "mktemp: слишком длинный шаблон\n");
            return 1;
        }
        if (l && tmpl[l - 1] == '.') snprintf(tmpl + l, sizeof(tmpl) - l, "XXXXXX");
        else                         snprintf(tmpl + l, sizeof(tmpl) - l, ".XXXXXX");
    }

    if (dir) {
        if (!mkdtemp(tmpl)) { perror("mktemp"); return 1; }
    } else {
        int fd = mkstemp(tmpl);
        if (fd < 0) { perror("mktemp"); return 1; }
        close(fd);
    }
    printf("%s\n", tmpl);
    return 0;
}

static int cmd_realpath(int argc, char **argv)
{
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1]) i++;
    if (i >= argc) { fprintf(stderr, "realpath: missing operand\n"); return 1; }

    int rc = 0;
    for (; i < argc; i++) {
        char *r = realpath(argv[i], NULL);
        if (!r) { perror(argv[i]); rc = 1; continue; }
        printf("%s\n", r);
        free(r);
    }
    return rc;
}

static int cmd_readlink(int argc, char **argv)
{
    int i = 1, canon = 0;
    if (i < argc && !strcmp(argv[i], "-f")) { canon = 1; i++; }
    if (i >= argc) { fprintf(stderr, "readlink: missing operand\n"); return 1; }

    int rc = 0;
    for (; i < argc; i++) {
        if (canon) {
            char *r = realpath(argv[i], NULL);
            if (!r) { perror(argv[i]); rc = 1; continue; }
            printf("%s\n", r);
            free(r);
        } else {
            char buf[PATH_MAX];
            ssize_t n = readlink(argv[i], buf, sizeof(buf) - 1);
            if (n < 0) { perror(argv[i]); rc = 1; continue; }
            buf[n] = 0;
            printf("%s\n", buf);
        }
    }
    return rc;
}

struct {
    const char *name;
    int (*fn)(int, char **);
} g_cmds[] = {
    {"true",     cmd_true},
    {"false",    cmd_false},
    {"yes",      cmd_yes},
    {"basename", cmd_basename},
    {"dirname",  cmd_dirname},
    {"mkdir",    cmd_mkdir},
    {"rmdir",    cmd_rmdir},
    {"rm",       cmd_rm},
    {"cp",       cmd_cp},
    {"mv",       cmd_mv},
    {"ln",       cmd_ln},
    {"chmod",    cmd_chmod},
    {"chown",    cmd_chown},
    {"sync",     cmd_sync},
    {"hostname", cmd_hostname},
    {"uptime",   cmd_uptime},
    {"reboot",   cmd_reboot},
    {"poweroff", cmd_poweroff},
    {"touch",    cmd_touch},
    {"df",       cmd_df},
    {"free",     cmd_free},
    {"mktemp",   cmd_mktemp},
    {"realpath", cmd_realpath},
    {"readlink", cmd_readlink},
    {NULL, NULL}
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: ybox <command> [args]\n");
        for (int i = 0; g_cmds[i].name; i++)
            fprintf(stderr, "  %s\n", g_cmds[i].name);
        return 2;
    }

    const char *cmd = argv[1];
    for (int i = 0; g_cmds[i].name; i++)
        if (!strcmp(g_cmds[i].name, cmd))
            return g_cmds[i].fn(argc - 1, argv + 1);

    fprintf(stderr, "ybox: unknown command: %s\n", cmd);
    return 127;
}
