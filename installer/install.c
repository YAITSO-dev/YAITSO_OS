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
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
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

    printf("Yaitso OS installer 0.3.1\n");
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

    int want_gui   = install_gui_ask();
    int want_glibc = install_glibc_ask();

    printf("\n");
    printf("Установка на %s\n", disk);
    printf("  Hostname: %s\n", hostname);
    printf("  User:     %s\n", user);
    printf("  GUI:      %s\n", want_gui ? "да" : "нет");
    printf("  glibc:    %s\n", want_glibc ? "да" : "нет");
    if (!ask_yes_no("Продолжить?", 0)) return 1;

    char rootp[160];
    snprintf(rootp, sizeof(rootp), "%s3", disk);

    char *mkfs_argv[] = { "mkfs.ext4", "-F", rootp, NULL };
    run_argv(mkfs_argv);

    const char *target = "/mnt/yaitso";
    mkdir(target, 0755);

    char *mnt_argv[] = { "mount", rootp, (char *)target, NULL };
    if (run_argv(mnt_argv) != 0) {
        fprintf(stderr, "installer: mount failed\n");
        return 1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/etc/hostname", target);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s\n", hostname); fclose(f); }

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
        run_argv(argv_glibc);
        char *argv_patch[] = { "chroot", (char *)target, "yat", "install",
                               "patchelf", NULL };
        run_argv(argv_patch);
    }

    char *ua_argv[] = { "chroot", (char *)target, "useradd", "-m",
                        "-G", "wheel", user, NULL };
    run_argv(ua_argv);

    char chpass_cmd[512];
    snprintf(chpass_cmd, sizeof(chpass_cmd),
             "echo '%s:%s' | chroot %s chpasswd", user, pass, target);
    char *sh_argv[] = { "sh", "-c", chpass_cmd, NULL };
    run_argv(sh_argv);

    char *grub_argv[] = { "chroot", (char *)target, "grub-install",
                          disk, NULL };
    run_argv(grub_argv);

    char *umnt_argv[] = { "umount", (char *)target, NULL };
    run_argv(umnt_argv);

    printf("\nУстановка завершена. Перезагрузка.\n");
    return 0;
}
