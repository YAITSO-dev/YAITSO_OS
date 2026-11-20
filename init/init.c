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
#include <dirent.h>
#include <grp.h>

#define TTYS "/etc/yaitso/tty.conf"
#define MAX_TTYS 8

static pid_t g_tty_pids[MAX_TTYS];
static char  g_tty_names[MAX_TTYS][16];
static int   g_nttys = 0;
static pid_t g_svc_pid = -1;

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

/* yaitso.install в командной строке ядра включает установщик на tty1. */
static int cmdline_has(const char *needle)
{
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) return 0;
    char buf[1024] = "";
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
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
static const char *boot_modules[] = { "bochs", "virtio_gpu",
                                      "psmouse", "usbhid", NULL };

static void mount_one(const char *src, const char *tgt, const char *type,
                      unsigned long flags, const char *data)
{
    if (mount(src, tgt, type, flags, data) != 0)
        fprintf(stderr, "init: mount %s on %s failed: %s\n", type, tgt,
                strerror(errno));
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

/* devtmpfs создаёт узлы устройств с правами root:root, а udev/mdev в
 * системе нет. Без этого обычный пользователь не может открыть DRM и
 * устройства ввода, и графическая сессия падает с «Permission denied».
 * Выставляем стандартные группы video/input/render/audio. */
static gid_t group_gid(const char *name, gid_t fallback)
{
    struct group *g = getgrnam(name);
    return g ? g->gr_gid : fallback;
}

static void dev_set(const char *dir, const char *name, gid_t gid, mode_t mode)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    if (chown(p, 0, gid) != 0) { /* узел мог исчезнуть — это не фатально */ }
    chmod(p, mode);
}

static void fix_dev_permissions(void)
{
    gid_t video  = group_gid("video", 44);
    gid_t input  = group_gid("input", 104);
    gid_t render = group_gid("render", 109);
    gid_t audio  = group_gid("audio", 29);

    DIR *d = opendir("/dev/dri");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strncmp(de->d_name, "card", 4))
                dev_set("/dev/dri", de->d_name, video, 0660);
            else if (!strncmp(de->d_name, "renderD", 7))
                dev_set("/dev/dri", de->d_name, render, 0660);
        }
        closedir(d);
    }

    d = opendir("/dev/input");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strncmp(de->d_name, "event", 5) ||
                !strncmp(de->d_name, "mouse", 5) ||
                !strcmp(de->d_name, "mice") ||
                !strncmp(de->d_name, "js", 2))
                dev_set("/dev/input", de->d_name, input, 0660);
        }
        closedir(d);
    }

    d = opendir("/dev/snd");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] != '.')
                dev_set("/dev/snd", de->d_name, audio, 0660);
        }
        closedir(d);
    }

    dev_set("/dev", "uinput", input, 0660);
    dev_set("/dev", "fb0", video, 0660);
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
    if (sethostname(h, strlen(h)) != 0)
        fprintf(stderr, "init: sethostname: %s\n", strerror(errno));
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
    char line[16];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        snprintf(ttys[n++], 16, "%s", line);
    }
    fclose(f);
    if (!n) { snprintf(ttys[0], 16, "tty1"); n = 1; }
    return n;
}

/* Запускает программу на tty, делая его управляющим терминалом. */
static pid_t spawn_on_tty(const char *tty, const char *path, char *const argv[])
{
    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/%s", tty);
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "init: cannot open %s: %s\n", dev, strerror(errno));
        return -1;
    }
    pid_t p = fork();
    if (p == 0) {
        setsid();
        ioctl(fd, TIOCSCTTY, 0);
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
        execv(path, argv);
        _exit(127);
    }
    close(fd);
    if (p < 0) fprintf(stderr, "init: fork failed: %s\n", strerror(errno));
    return p;
}

static pid_t spawn_getty(const char *tty)
{
    char *argv[] = { "/sbin/getty", (char *)tty, NULL };
    return spawn_on_tty(tty, "/sbin/getty", argv);
}

static pid_t spawn_installer(const char *tty)
{
    char *argv[] = { "/usr/bin/yaitso-install", NULL };
    return spawn_on_tty(tty, "/usr/bin/yaitso-install", argv);
}

static pid_t spawn_daemon(void)
{
    pid_t p = fork();
    if (p == 0) {
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
                       if (fd > 2) close(fd); }
        char *argv[] = { "/usr/bin/svc", "daemon", NULL };
        execv("/usr/bin/svc", argv);
        _exit(127);
    }
    return p;
}

int main(void)
{
    if (getpid() != 1) { fprintf(stderr, "init: not PID 1\n"); return 1; }

    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP,  SIG_IGN);

    /* 1. pseudo-filesystems first: modprobe needs /sys and /dev, and the
     *    devtmpfs nodes (/dev/dri/card0) must exist when a driver binds. */
    mount_all();

    /* 2. GPU drivers, synchronously, before any getty or the GUI starts. */
    for (int i = 0; boot_modules[i]; i++)
        load_module(boot_modules[i]);
    fix_dev_permissions();

    set_hostname();

    mkdir_p("/var/log", 0755);
    mkdir_p("/var/log/svc", 0755);
    /* /run/user должен позволять обычному пользователю создать свой
     * XDG_RUNTIME_DIR. Иначе start-desktop не может сделать /run/user/UID,
     * и ycomp падает с «ipc init failed». */
    mkdir_p("/run/user", 01777);
    chmod("/run/user", 01777);

    /* desktop/text определяет только, что запустит login после
     * аутентификации. init всегда даёт getty — иначе GUI работал бы от root
     * без входа в систему. */
    (void)is_desktop_session();

    g_svc_pid = spawn_daemon();

    g_nttys = read_ttys(g_tty_names, MAX_TTYS);
    if (g_nttys > MAX_TTYS) g_nttys = MAX_TTYS;

    int install_mode = cmdline_has("yaitso.install");
    int installer_done = 0;

    for (int i = 0; i < g_nttys; i++) {
        if (i == 0 && install_mode && !installer_done)
            g_tty_pids[i] = spawn_installer(g_tty_names[i]);
        else
            g_tty_pids[i] = spawn_getty(g_tty_names[i]);
    }

    /* PID 1: перезапускаем getty после logout/crash. Это делает сессию
     * нормальной: после выхода из рабочего стола снова появляется login. */
    for (;;) {
        int st = 0;
        pid_t p = waitpid(-1, &st, 0);
        if (p < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (p == g_svc_pid) {
            sleep(1);
            g_svc_pid = spawn_daemon();
            continue;
        }
        for (int i = 0; i < g_nttys; i++) {
            if (p != g_tty_pids[i]) continue;
            if (i == 0 && install_mode && !installer_done) {
                installer_done = 1;
                g_tty_pids[i] = spawn_getty(g_tty_names[i]);
            } else {
                g_tty_pids[i] = spawn_getty(g_tty_names[i]);
            }
            break;
        }
    }
    return 0;
}
