#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sched.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#define SVC_DIR     "/etc/yaitso/services"
#define RUN_DIR     "/run/svc"
#define LOG_DIR     "/var/log/svc"

struct service {
    char name[64];
    char exec[512];
    char restart[16];
    char enabled[8];
    char oneshot[8];
    int  timeout;
    char explain[256];
    char user[64];
    char sandbox[16];
};

static volatile sig_atomic_t g_stop = 0;
static pid_t g_children[128];
static int   g_nchildren = 0;

static void on_signal(int s) { (void)s; g_stop = 1; }

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' ||
                     e[-1] == ' '  || e[-1] == '\t')) *--e = 0;
    return s;
}

static int valid_svc_name(const char *n)
{
    if (!n || !*n || strlen(n) >= 64) return 0;
    if (!strcmp(n, ".") || !strcmp(n, "..")) return 0;
    for (const char *p = n; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.'))
            return 0;
    return 1;
}

static int parse_svc(const char *path, struct service *sv)
{
    memset(sv, 0, sizeof(*sv));
    sv->timeout = 10;
    snprintf(sv->restart, sizeof(sv->restart), "no");

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p);
        char *v = trim(eq + 1);

        if      (!strcmp(k, "name"))    snprintf(sv->name, sizeof(sv->name), "%s", v);
        else if (!strcmp(k, "exec"))    snprintf(sv->exec, sizeof(sv->exec), "%s", v);
        else if (!strcmp(k, "restart")) snprintf(sv->restart, sizeof(sv->restart), "%s", v);
        else if (!strcmp(k, "enabled")) snprintf(sv->enabled, sizeof(sv->enabled), "%s", v);
        else if (!strcmp(k, "oneshot")) snprintf(sv->oneshot, sizeof(sv->oneshot), "%s", v);
        else if (!strcmp(k, "timeout")) sv->timeout = atoi(v);
        else if (!strcmp(k, "explain")) snprintf(sv->explain, sizeof(sv->explain), "%s", v);
        else if (!strcmp(k, "user"))    snprintf(sv->user, sizeof(sv->user), "%s", v);
        else if (!strcmp(k, "sandbox")) snprintf(sv->sandbox, sizeof(sv->sandbox), "%s", v);
    }
    fclose(f);
    return valid_svc_name(sv->name) ? 0 : -1;
}

static int read_pid(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", RUN_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int pid = -1;
    if (fscanf(f, "%d", &pid) != 1) pid = -1;
    fclose(f);
    return pid;
}

static int write_pid(const char *name, pid_t pid)
{
    mkdir(RUN_DIR, 0755);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", RUN_DIR, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", (int)pid);
    fclose(f);
    return 0;
}

static void remove_pid(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", RUN_DIR, name);
    unlink(path);
}

static int is_running(const char *name)
{
    int pid = read_pid(name);
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    if (errno == EPERM) return 1;
    remove_pid(name);
    return 0;
}

static void build_log_paths(const char *name, char *out, char *err, size_t n)
{
    mkdir(LOG_DIR, 0755);
    snprintf(out, n, "%s/%s.out.log", LOG_DIR, name);
    snprintf(err, n, "%s/%s.err.log", LOG_DIR, name);
}

static int do_sandbox(void)
{
    if (unshare(CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS) != 0) return -1;
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    mkdir("/tmp/svc-tmp", 01777);
    mount("tmpfs", "/tmp/svc-tmp", "tmpfs", 0, "mode=1777,nosuid,nodev");
    return 0;
}

static int drop_user(const char *user)
{
    struct passwd *pw = getpwnam(user);
    if (!pw) return -1;
    if (initgroups(user, pw->pw_gid) != 0) return -1;
    if (setgid(pw->pw_gid) != 0) return -1;
    if (setuid(pw->pw_uid) != 0) return -1;
    return 0;
}

static pid_t spawn_service(const struct service *sv)
{
    char *argv[128];
    int n = 0;
    char *copy = strdup(sv->exec);
    char *save = NULL;
    char *tok = strtok_r(copy, " \t", &save);
    while (tok && n < 127) { argv[n++] = tok; tok = strtok_r(NULL, " \t", &save); }
    argv[n] = NULL;
    if (n == 0) { free(copy); return -1; }

    pid_t pid = fork();
    if (pid < 0) { free(copy); return -1; }
    if (pid == 0) {
        setsid();
        if (sv->sandbox[0] && !strcmp(sv->sandbox, "yes")) do_sandbox();

        char out_path[256], err_path[256];
        build_log_paths(sv->name, out_path, err_path, sizeof(out_path));

        int out_fd = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        int err_fd = open(err_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out_fd >= 0) { dup2(out_fd, 1); if (out_fd > 2) close(out_fd); }
        if (err_fd >= 0) { dup2(err_fd, 2); if (err_fd > 2) close(err_fd); }

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); if (devnull > 2) close(devnull); }

        if (sv->user[0] && drop_user(sv->user) != 0) _exit(126);

        execvp(argv[0], argv);
        _exit(127);
    }

    free(copy);
    write_pid(sv->name, pid);
    return pid;
}

static int svc_start(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.svc", SVC_DIR, name);
    struct service sv;
    if (parse_svc(path, &sv) != 0) {
        fprintf(stderr, "svc: %s не найден\n", name);
        return 1;
    }
    if (is_running(sv.name)) {
        fprintf(stderr, "svc: %s уже запущен\n", sv.name);
        return 0;
    }
    pid_t pid = spawn_service(&sv);
    if (pid < 0) { fprintf(stderr, "svc: fork failed\n"); return 1; }
    printf("svc: %s started (pid %d)\n", sv.name, (int)pid);
    return 0;
}

static int svc_stop(const char *name)
{
    int pid = read_pid(name);
    if (pid <= 0) { fprintf(stderr, "svc: %s не запущен\n", name); return 1; }
    if (kill(pid, SIGTERM) != 0) { perror("kill"); return 1; }

    for (int i = 0; i < 50; i++) {
        if (kill(pid, 0) != 0) { remove_pid(name); return 0; }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    remove_pid(name);
    return 0;
}

static int svc_status(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.svc", SVC_DIR, name);
    struct service sv;
    if (parse_svc(path, &sv) != 0) {
        fprintf(stderr, "svc: %s не найден\n", name);
        return 1;
    }
    int pid = read_pid(name);
    printf("service: %s\n", sv.name);
    printf("exec:    %s\n", sv.exec);
    printf("enabled: %s\n", sv.enabled[0] ? sv.enabled : "no");
    printf("restart: %s\n", sv.restart);
    printf("explain: %s\n", sv.explain);
    printf("status:  %s\n",
           (pid > 0 && kill(pid, 0) == 0) ? "running" : "stopped");
    if (pid > 0) printf("pid:     %d\n", pid);
    return 0;
}

static int svc_enable(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.svc", SVC_DIR, name);
    if (access(path, R_OK) != 0) { fprintf(stderr, "svc: %s не найден\n", name); return 1; }

    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char tmp[4096];
    size_t n = fread(tmp, 1, sizeof(tmp) - 1, f);
    fclose(f);
    tmp[n] = 0;

    FILE *o = fopen(path, "w");
    if (!o) return 1;
    char *line = strtok(tmp, "\n");
    int done = 0;
    while (line) {
        if (!strncmp(line, "enabled", 7)) {
            fprintf(o, "enabled = yes\n");
            done = 1;
        } else {
            fprintf(o, "%s\n", line);
        }
        line = strtok(NULL, "\n");
    }
    if (!done) fprintf(o, "enabled = yes\n");
    fclose(o);
    printf("svc: %s enabled\n", name);
    return 0;
}

static int svc_disable(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.svc", SVC_DIR, name);
    if (access(path, R_OK) != 0) { fprintf(stderr, "svc: %s не найден\n", name); return 1; }

    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char tmp[4096];
    size_t n = fread(tmp, 1, sizeof(tmp) - 1, f);
    fclose(f);
    tmp[n] = 0;

    FILE *o = fopen(path, "w");
    if (!o) return 1;
    char *line = strtok(tmp, "\n");
    int done = 0;
    while (line) {
        if (!strncmp(line, "enabled", 7)) {
            fprintf(o, "enabled = no\n");
            done = 1;
        } else {
            fprintf(o, "%s\n", line);
        }
        line = strtok(NULL, "\n");
    }
    if (!done) fprintf(o, "enabled = no\n");
    fclose(o);
    printf("svc: %s disabled\n", name);
    return 0;
}

static int svc_list(void)
{
    DIR *d = opendir(SVC_DIR);
    if (!d) { fprintf(stderr, "svc: %s недоступен\n", SVC_DIR); return 1; }
    struct dirent *de;
    printf("%-20s %-10s %s\n", "NAME", "STATUS", "DESCRIPTION");
    while ((de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 4 || strcmp(de->d_name + l - 4, ".svc")) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", SVC_DIR, de->d_name);
        struct service sv;
        if (parse_svc(path, &sv) != 0) continue;
        int running = is_running(sv.name);
        printf("%-20s %-10s %s\n", sv.name,
               running ? "running" : "stopped", sv.explain);
    }
    closedir(d);
    return 0;
}

static int svc_daemon(void)
{
    signal(SIGCHLD, SIG_DFL);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    DIR *d = opendir(SVC_DIR);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 4 || strcmp(de->d_name + l - 4, ".svc")) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", SVC_DIR, de->d_name);
        struct service sv;
        if (parse_svc(path, &sv) != 0) continue;
        if (!sv.enabled[0] || strcmp(sv.enabled, "yes")) continue;
        if (sv.oneshot[0] && !strcmp(sv.oneshot, "yes")) {
            pid_t pid = spawn_service(&sv);
            if (pid > 0) {
                int st = 0;
                waitpid(pid, &st, 0);
                remove_pid(sv.name);
            }
            continue;
        }
        pid_t pid = spawn_service(&sv);
        if (pid > 0) {
            if (g_nchildren < 128) g_children[g_nchildren++] = pid;
            printf("svc: started %s (pid %d)\n", sv.name, (int)pid);
        }
    }
    closedir(d);

    while (!g_stop) {
        int st = 0;
        pid_t p = waitpid(-1, &st, 0);
        if (p < 0) break;
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "svc 0.3.2 — сервис-менеджер Yaitso OS\n"
        "  svc list             — список сервисов\n"
        "  svc start <name>     — запустить\n"
        "  svc stop <name>      — остановить\n"
        "  svc status <name>    — статус\n"
        "  svc enable <name>    — включить автозапуск\n"
        "  svc disable <name>   — выключить автозапуск\n"
        "  svc daemon           — фоновый режим (для init)\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "list"))    return svc_list();
    if (!strcmp(cmd, "daemon"))  return svc_daemon();
    if (argc < 3) { usage(); return 2; }
    if (!valid_svc_name(argv[2])) {
        fprintf(stderr, "svc: недопустимое имя сервиса: %s\n", argv[2]);
        return 2;
    }

    if (!strcmp(cmd, "start"))   return svc_start(argv[2]);
    if (!strcmp(cmd, "stop"))    return svc_stop(argv[2]);
    if (!strcmp(cmd, "status"))  return svc_status(argv[2]);
    if (!strcmp(cmd, "enable"))  return svc_enable(argv[2]);
    if (!strcmp(cmd, "disable")) return svc_disable(argv[2]);

    usage();
    return 2;
}
