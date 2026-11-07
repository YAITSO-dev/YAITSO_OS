#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <termios.h>

#define TTYS "/etc/yaitso/tty.conf"
#define MAX_TTYS 8

/* Консоли: на каждой висит getty; при выходе getty init поднимает его заново. */
static pid_t g_getty_pid[MAX_TTYS];
static char  g_tty_name[MAX_TTYS][16];
static int   g_ntty = 0;

static void mkdir_p(const char *p, mode_t m)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", p);
    for (char *q = tmp + 1; *q; q++) {
        if (*q == '/') { *q = 0; mkdir(tmp, m); *q = '/'; }
    }
    mkdir(tmp, m);
}

static void mount_all(void)
{
    mkdir_p("/proc", 0755);
    mkdir_p("/sys",  0755);
    mkdir_p("/dev",  0755);
    mkdir_p("/run",  0755);
    mkdir_p("/tmp",  01777);
    mount("proc",     "/proc", "proc",     0, NULL);
    mount("sysfs",    "/sys",  "sysfs",    0, NULL);
    mount("devtmpfs", "/dev",  "devtmpfs", 0, NULL);
    mount("tmpfs",    "/run",  "tmpfs",    0, "mode=0755,nosuid,nodev");
    mount("tmpfs",    "/tmp",  "tmpfs",    0, "mode=1777");
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, "mode=0620,gid=5");
    mkdir("/dev/shm", 01777);
    mount("tmpfs", "/dev/shm", "tmpfs", 0, "mode=1777");
}

static void set_hostname(void)
{
    FILE *f = fopen("/etc/hostname", "r");
    if (!f) return;
    char h[64] = "yaitso";
    if (fgets(h, sizeof(h), f)) h[strcspn(h, "\r\n")] = 0;
    fclose(f);
    sethostname(h, strlen(h));
}

/* Имя консоли: только tty + цифры, чтобы в /dev не уходило ничего лишнего. */
static int valid_tty_name(const char *s)
{
    if (strlen(s) >= 16) return 0;
    if (strncmp(s, "tty", 3) != 0 || !s[3]) return 0;
    for (const char *p = s + 3; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int read_ttys(void)
{
    FILE *f = fopen(TTYS, "r");
    int n = 0;
    if (!f) {
        for (int i = 1; i <= 4; i++) snprintf(g_tty_name[n++], 16, "tty%d", i);
        return n;
    }
    char line[32];
    while (n < MAX_TTYS && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!valid_tty_name(line)) continue;
        snprintf(g_tty_name[n++], 16, "%s", line);
    }
    fclose(f);
    if (!n) { snprintf(g_tty_name[0], 16, "tty1"); n = 1; }
    return n;
}

/* Запуск getty на консоли i. Сам getty получает терминал через setsid + TIOCSCTTY. */
static void spawn_getty(int i)
{
    pid_t p = fork();
    if (p < 0) { g_getty_pid[i] = -1; return; }
    if (p == 0) {
        char dev[32];
        snprintf(dev, sizeof(dev), "/dev/%s", g_tty_name[i]);
        setsid();
        int fd = open(dev, O_RDWR);
        if (fd < 0) _exit(1);
        ioctl(fd, TIOCSCTTY, 0);
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
        char *argv[] = { "/sbin/getty", g_tty_name[i], NULL };
        execv("/sbin/getty", argv);
        _exit(127);
    }
    g_getty_pid[i] = p;
}

static pid_t spawn_service(const char *path, char *const argv[])
{
    pid_t p = fork();
    if (p == 0) {
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        execv(path, argv);
        _exit(127);
    }
    return p;
}

int main(void)
{
    pid_t mod_pid = fork();
    if (mod_pid == 0) {
        execl("/sbin/modprobe", "modprobe", "bochs", (char *)NULL);
        _exit(1);
    }
    if (mod_pid > 0) waitpid(mod_pid, NULL, 0);
    if (getpid() != 1) { fprintf(stderr, "init: not PID 1\n"); return 1; }

    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    mount_all();
    set_hostname();

    mkdir_p("/var/log", 0755);
    mkdir_p("/var/log/svc", 0755);
    mkdir_p("/run/user", 0755);

    char *svc_argv[] = { "/bin/svc", "daemon", NULL };
    spawn_service("/bin/svc", svc_argv);

    /* Вход всегда через login (getty): GUI на tty1 запускает сам login, после пароля. */
    g_ntty = read_ttys();
    for (int i = 0; i < g_ntty; i++) spawn_getty(i);

    for (;;) {
        int st = 0;
        pid_t p = waitpid(-1, &st, 0);
        if (p < 0) {
            if (errno == ECHILD) pause();
            continue;
        }
        for (int i = 0; i < g_ntty; i++) {
            if (g_getty_pid[i] == p) {
                sleep(1);            /* не крутим getty без паузы, если он падает сразу */
                spawn_getty(i);
                break;
            }
        }
    }
    return 0;
}
