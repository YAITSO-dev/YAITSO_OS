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

static const char *prog = "ybox";

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

/* Копирование без шелла: имена файлов никогда не интерпретируются. */
static int copy_file_data(const char *src, const char *dst, mode_t mode)
{
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) { perror(src); return 1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode & 07777);
    if (out < 0) { perror(dst); close(in); return 1; }
    char buf[65536];
    int rc = 0;
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) { perror(src); rc = 1; break; }
        if (r == 0) break;
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) { perror(dst); rc = 1; goto done; }
            off += w;
        }
    }
done:
    fchmod(out, mode & 07777);
    close(in);
    if (close(out) != 0 && rc == 0) { perror(dst); rc = 1; }
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
        if (!recursive) { fprintf(stderr, "cp: %s: is a directory\n", src); return 1; }
        if (mkdir(dst, st.st_mode & 07777) != 0 && errno != EEXIST) { perror(dst); return 1; }
        DIR *d = opendir(src);
        if (!d) { perror(src); return 1; }
        int rc = 0;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s2[PATH_MAX], d2[PATH_MAX];
            int n1 = snprintf(s2, sizeof(s2), "%s/%s", src, de->d_name);
            int n2 = snprintf(d2, sizeof(d2), "%s/%s", dst, de->d_name);
            if (n1 < 0 || n2 < 0 || n1 >= (int)sizeof(s2) || n2 >= (int)sizeof(d2)) {
                fprintf(stderr, "cp: path too long\n");
                rc = 1;
                continue;
            }
            if (copy_path(s2, d2, 1) != 0) rc = 1;
        }
        closedir(d);
        chmod(dst, st.st_mode & 07777);
        return rc;
    }
    if (S_ISREG(st.st_mode)) return copy_file_data(src, dst, st.st_mode);
    fprintf(stderr, "cp: %s: unsupported file type\n", src);
    return 1;
}

static int cmd_cp(int argc, char **argv)
{
    int recursive = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (strchr(argv[i] + 1, 'r') || strchr(argv[i] + 1, 'R') || strchr(argv[i] + 1, 'a'))
            recursive = 1;
    }
    if (argc - i < 2) { fprintf(stderr, "cp: missing operand\n"); return 1; }

    const char *src = argv[i];
    const char *dst = argv[i + 1];

    char target[PATH_MAX];
    struct stat dst_st;
    if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        if (snprintf(target, sizeof(target), "%s/%s", dst, base) >= (int)sizeof(target)) {
            fprintf(stderr, "cp: path too long\n");
            return 1;
        }
    } else {
        snprintf(target, sizeof(target), "%s", dst);
    }
    return copy_path(src, target, recursive);
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

static int cmd_chown(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "chown: usage: chown USER FILE...\n"); return 1; }
    const char *user = argv[1];
    char *colon = strchr(user, ':');
    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    if (colon) {
        *colon = 0;
        uid = (uid_t)atoi(user);
        gid = (gid_t)atoi(colon + 1);
    } else {
        uid = (uid_t)atoi(user);
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
