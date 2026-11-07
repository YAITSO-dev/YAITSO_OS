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

static pid_t g_children[64];
static int   g_nchildren = 0;

static int is_desktop_session(void)
{
    FILE *f = fopen("/etc/yaitso/session", "r");
    if (!f) return 0;
    char buf[32] = "";
    int r = 0;
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (!strcmp(buf, "desktop")) r = 1;
    }
    fclose(f);
    return r;
}

static void mkdir_p(const char *p, mode_t m)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", p);
    for (char *q = tmp + 1; *q; q++) {
        if (*q == '/') { *q = 0; mkdir(tmp, m); *q = '/'; }
    }
    mkdir(tmp, m);
}


#define MODPROBE "/sbin/modprobe"

/* Modules loaded at boot, in order. bochs: QEMU -vga std (PCI 1234:1111).
 * virtio_gpu: QEMU -vga virtio (PCI 1af4:1050). A module that was not shipped
 * is reported and skipped, it is not fatal. */
static const char *boot_modules[] = { "bochs", "virtio_gpu", NULL };

static void mount_one(const char *src, const char *tgt, const char *type,
                      unsigned long flags, const char *data)
{
    if (mount(src, tgt, type, flags, data) != 0)
        fprintf(stderr, "init: mount %s on %s failed: %s\n", type, tgt, strerror(errno));
}

static void load_module(const char *name)
{
    pid_t p = fork();
    if (p < 0) {
        fprintf(stderr, "init: fork for modprobe %s failed\n", name);
        return;
    }
    if (p == 0) {
        execl(MODPROBE, "modprobe", name, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
        fprintf(stderr, "init: modprobe %s: ok\n", name);
    else
        fprintf(stderr, "init: modprobe %s: failed (exit %d)\n", name,
                WIFEXITED(st) ? WEXITSTATUS(st) : -1);
}

static void mount_all(void)
{
    mkdir_p("/proc", 0755);
    mkdir_p("/sys",  0755);
    mkdir_p("/dev",  0755);
    mkdir_p("/run",  0755);
    mkdir_p("/tmp",  01777);
    mount_one("proc",     "/proc", "proc",     0, NULL);
    mount_one("sysfs",    "/sys",  "sysfs",    0, NULL);
    mount_one("devtmpfs", "/dev",  "devtmpfs", 0, NULL);
    mount_one("tmpfs",    "/run",  "tmpfs",    MS_NOSUID | MS_NODEV, "mode=0755");
    mount_one("tmpfs",    "/tmp",  "tmpfs",    0, "mode=1777");
    mkdir("/dev/pts", 0755);
    mount_one("devpts",   "/dev/pts", "devpts", 0, "mode=0620,gid=5");
    mkdir("/dev/shm", 01777);
    mount_one("tmpfs",    "/dev/shm", "tmpfs",  0, "mode=1777");
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

static void reap(int sig)
{
    (void)sig;
    int st;
    while (waitpid(-1, &st, WNOHANG) > 0) {}
}

static void spawn(const char *path, char *const argv[])
{
    pid_t p = fork();
    if (p < 0) return;
    if (p == 0) {
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        execv(path, argv);
        _exit(127);
    }
    if (g_nchildren < (int)(sizeof(g_children) / sizeof(g_children[0])))
        g_children[g_nchildren++] = p;
}

static int read_ttys(char ttys[][16], int max)
{
    FILE *f = fopen(TTYS, "r");
    if (!f) {
        snprintf(ttys[0], 16, "tty1");
        snprintf(ttys[1], 16, "tty2");
        snprintf(ttys[2], 16, "tty3");
        snprintf(ttys[3], 16, "tty4");
        return 4;
    }
    int n = 0;
    char line[32];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        snprintf(ttys[n++], 16, "%s", line);
    }
    fclose(f);
    if (!n) { snprintf(ttys[0], 16, "tty1"); n = 1; }
    return n;
}

static void spawn_getty(const char *tty)
{
    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/%s", tty);
    int fd = open(dev, O_RDWR);
    if (fd < 0) return;
    pid_t p = fork();
    if (p == 0) {
        setsid();
        ioctl(fd, TIOCSCTTY, 0);
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
        char *argv[] = { "/sbin/getty", (char *)tty, NULL };
        execv("/sbin/getty", argv);
        _exit(127);
    }
    if (p > 0 && g_nchildren < (int)(sizeof(g_children) / sizeof(g_children[0])))
        g_children[g_nchildren++] = p;
    close(fd);
}

int main(void)
{
    if (getpid() != 1) { fprintf(stderr, "init: not PID 1\n"); return 1; }

    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    /* 1. pseudo-filesystems first: modprobe needs /sys and /dev, and the
     *    devtmpfs nodes (/dev/dri/card0) must exist when a driver binds. */
    mount_all();

    /* 2. GPU drivers, synchronously, before any getty or the GUI starts.
     *    The SIGCHLD reaper is installed only after this, so waitpid() here
     *    cannot lose its child to the reaper. */
    for (int i = 0; boot_modules[i]; i++)
        load_module(boot_modules[i]);

    signal(SIGCHLD, reap);

    set_hostname();

    mkdir_p("/var/log", 0755);
    mkdir_p("/var/log/svc", 0755);
    mkdir_p("/run/user", 0755);

    int desktop = is_desktop_session();

    char *svc_argv[] = { "/usr/bin/svc", "daemon", NULL };
    spawn("/usr/bin/svc", svc_argv);

    char ttys[MAX_TTYS][16];
    int n = read_ttys(ttys, MAX_TTYS);
    (void)desktop;
    // На tty1 запускаем GUI
    pid_t gui_pid = fork();
    if (gui_pid == 0) {
        setsid();
        int fd = open("/dev/tty1", O_RDWR);
        if (fd >= 0) {
            ioctl(fd, TIOCSCTTY, 0);
            dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
            if (fd > 2) close(fd);
        }
        execl("/usr/bin/start-desktop", "start-desktop", (char *)NULL);
        // Если start-desktop упал, запускаем консоль
        execl("/usr/bin/yaitso", "yaitso", (char *)NULL);
        _exit(127);
    }
    if (gui_pid > 0 && g_nchildren < (int)(sizeof(g_children) / sizeof(g_children[0])))
        g_children[g_nchildren++] = gui_pid;

    // На остальных tty (начиная со второй) — обычный getty
    for (int i = 1; i < n; i++)
        spawn_getty(ttys[i]);

    for (;;) pause();
    return 0;
}
