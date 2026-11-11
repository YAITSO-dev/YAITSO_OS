#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <errno.h>
#include <ctype.h>

static int run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp(argv[0], argv); _exit(127); }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* chpasswd читает user:pass из stdin. Пароль не должен содержать
 * ':' или перевод строки — иначе формат ломается. */
static int run_chpasswd(const char *target, const char *user, const char *pass)
{
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[1]);
        dup2(pfd[0], 0);
        close(pfd[0]);
        execl("/usr/sbin/chroot", "chroot", target,
              "/sbin/busybox", "chpasswd", (char *)NULL);
        _exit(127);
    }
    close(pfd[0]);
    char line[256];
    int n = snprintf(line, sizeof(line), "%s:%s\n", user, pass);
    int ok = 0;
    if (n > 0 && (size_t)n < sizeof(line)) {
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(pfd[1], line + off, (size_t)n - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        ok = (off == (size_t)n);
    }
    close(pfd[1]);
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (!ok || !WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1;
    return 0;
}

static int ask_yes_no(const char *prompt, int def)
{
    char buf[16];
    printf("%s [%s]: ", prompt, def ? "Y/n" : "y/N");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return def;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!buf[0]) return def;
    return buf[0] == 'y' || buf[0] == 'Y';
}

static int valid_username(const char *s)
{
    if (!s[0]) return 0;
    if (!(s[0] >= 'a' && s[0] <= 'z') && s[0] != '_') return 0;
    for (const char *p = s; *p; p++)
        if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) ||
              *p == '_' || *p == '-')) return 0;
    return 1;
}

static int install_gui_ask(void)
{
    printf("\n");
    printf("  Установить графическую среду?\n");
    printf("  Pre-alpha: wlroots + ycomp + yterm + ybar + ylaunch\n");
    printf("  Без GPU, без звука, без браузера.\n");
    return ask_yes_no("  Установить?", 0);
}

static int install_glibc_ask(void)
{
    printf("\n");
    printf("  Установить glibc-совместимость?\n");
    printf("  Позволяет ставить Firefox, Chromium, Steam и другие\n");
    printf("  prebuilt-приложения через yat.\n");
    return ask_yes_no("  Установить?", 1);
}

static int write_session(const char *target, const char *val)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/etc/yaitso", target);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/etc/yaitso/session", target);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", val);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("Yaitso OS installer 0.3.2\n");
    printf("==========================\n\n");

    char disk[128];
    printf("Диск для установки [/dev/vda]: ");
    fflush(stdout);
    if (!fgets(disk, sizeof(disk), stdin)) return 1;
    disk[strcspn(disk, "\r\n")] = 0;
    if (!disk[0]) snprintf(disk, sizeof(disk), "/dev/vda");

    char hostname[64];
    printf("Hostname [yaitso]: ");
    fflush(stdout);
    if (!fgets(hostname, sizeof(hostname), stdin)) return 1;
    hostname[strcspn(hostname, "\r\n")] = 0;
    if (!hostname[0]) snprintf(hostname, sizeof(hostname), "yaitso");

    char user[64];
    do {
        printf("Username: ");
        fflush(stdout);
        if (!fgets(user, sizeof(user), stdin)) return 1;
        user[strcspn(user, "\r\n")] = 0;
    } while (!valid_username(user));

    char pass[128];
    printf("Password: ");
    fflush(stdout);
    if (!fgets(pass, sizeof(pass), stdin)) return 1;
    pass[strcspn(pass, "\r\n")] = 0;
    if (!pass[0] || strchr(pass, ':') || strchr(pass, '\n')) {
        fprintf(stderr, "installer: недопустимый пароль\n");
        return 1;
    }

    int want_gui   = install_gui_ask();
    int want_glibc = install_glibc_ask();

    printf("\n");
    printf("Установка на %s\n", disk);
    printf("  Hostname: %s\n", hostname);
    printf("  User:     %s\n", user);
    printf("  GUI:      %s\n", want_gui ? "да" : "нет");
    printf("  glibc:    %s\n", want_glibc ? "да" : "нет");
    if (!ask_yes_no("Продолжить?", 0)) return 1;

    /* NVMe/mmcblk используют разделитель 'p' перед номером раздела. */
    char rootp[160];
    size_t dl = strlen(disk);
    if (dl && isdigit((unsigned char)disk[dl - 1]))
        snprintf(rootp, sizeof(rootp), "%sp3", disk);
    else
        snprintf(rootp, sizeof(rootp), "%s3", disk);

    char *mkfs_argv[] = { "mkfs.ext4", "-F", rootp, NULL };
    if (run_argv(mkfs_argv) != 0) {
        fprintf(stderr, "installer: mkfs.ext4 failed on %s\n", rootp);
        return 1;
    }

    const char *target = "/mnt/yaitso";
    mkdir(target, 0755);

    char *mnt_argv[] = { "mount", rootp, (char *)target, NULL };
    if (run_argv(mnt_argv) != 0) {
        fprintf(stderr, "installer: mount failed\n");
        return 1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/etc", target);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/etc/hostname", target);
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "installer: не удалось записать %s: %s\n", path,
                strerror(errno));
        char *umnt_err[] = { "umount", (char *)target, NULL };
        run_argv(umnt_err);
        return 1;
    }
    fprintf(f, "%s\n", hostname);
    fclose(f);

    if (want_gui) {
        char *argv_gui[] = { "chroot", (char *)target, "yat", "install",
                             "yaitso-desktop", NULL };
        run_argv(argv_gui);
        write_session(target, "desktop");
    } else {
        write_session(target, "text");
    }

    if (want_glibc) {
        char *argv_glibc[] = { "chroot", (char *)target, "yat", "install",
                               "glibc", NULL };
        if (run_argv(argv_glibc) != 0)
            fprintf(stderr, "installer: предупреждение: glibc не установлен\n");
        char *argv_patch[] = { "chroot", (char *)target, "yat", "install",
                               "patchelf", NULL };
        if (run_argv(argv_patch) != 0)
            fprintf(stderr, "installer: предупреждение: patchelf не установлен\n");
    }

    char *ua_argv[] = { "chroot", (char *)target, "useradd", "-m",
                        "-G", "wheel", user, NULL };
    if (run_argv(ua_argv) != 0) {
        fprintf(stderr, "installer: useradd failed\n");
        char *umnt_err[] = { "umount", (char *)target, NULL };
        run_argv(umnt_err);
        return 1;
    }

    if (run_chpasswd(target, user, pass) != 0) {
        fprintf(stderr, "installer: chpasswd failed\n");
        char *umnt_err[] = { "umount", (char *)target, NULL };
        run_argv(umnt_err);
        return 1;
    }

    char *grub_argv[] = { "chroot", (char *)target, "grub-install",
                          disk, NULL };
    if (run_argv(grub_argv) != 0)
        fprintf(stderr, "installer: предупреждение: grub-install failed\n");

    char *umnt_argv[] = { "umount", (char *)target, NULL };
    if (run_argv(umnt_argv) != 0)
        fprintf(stderr, "installer: предупреждение: umount failed\n");

    printf("\nУстановка завершена. Перезагрузка.\n");
    return 0;
}
