#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <termios.h>
#include <crypt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <linux/fs.h>

#define INSTALLER_VERSION "0.3.3"
#define TARGET "/mnt/yaitso"
#define MIN_DISK_BYTES (4ULL * 1024 * 1024 * 1024)

static int g_uefi = 0;
static int g_yes = 0;
static int g_force = 0;
static int g_root_mounted = 0;
static int g_esp_mounted = 0;
static int g_bind_dev = 0, g_bind_proc = 0, g_bind_sys = 0, g_bind_run = 0;

static void print_header(void)
{
    printf("\n");
    printf("  Yaitso OS %s installer\n", INSTALLER_VERSION);
    printf("  ============================\n\n");
}

static void die(const char *msg)
{
    fprintf(stderr, "\ninstaller: ОШИБКА: %s\n", msg);
    if (errno) fprintf(stderr, "installer: %s\n", strerror(errno));
}

static int run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "installer: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Запуск команды с stdin из строки. */
static int run_input(char *const argv[], const char *input)
{
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[1]);
        dup2(pfd[0], 0);
        close(pfd[0]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pfd[0]);
    size_t n = input ? strlen(input) : 0, off = 0;
    while (off < n) {
        ssize_t w = write(pfd[1], input + off, n - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(pfd[1]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return (off == n && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
}

/* Выполнить команду внутри установленной системы. */
static int run_chroot(const char *target, const char *prog,
                      char *const argv[], const char *input)
{
    int pfd[2] = {-1, -1};
    if (input && pipe(pfd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        if (input) { close(pfd[0]); close(pfd[1]); }
        return -1;
    }
    if (pid == 0) {
        if (input) {
            close(pfd[1]);
            dup2(pfd[0], 0);
            close(pfd[0]);
        }
        if (chdir(target) != 0) _exit(126);
        if (chroot(target) != 0) _exit(126);
        if (chdir("/") != 0) _exit(126);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:"
                       "/sbin:/bin", 1);
        setenv("HOME", "/root", 1);
        setenv("TERM", "linux", 1);
        execv(prog, argv);
        fprintf(stderr, "installer: chroot %s: %s\n", prog, strerror(errno));
        _exit(127);
    }
    if (input) {
        close(pfd[0]);
        size_t n = strlen(input), off = 0;
        while (off < n) {
            ssize_t w = write(pfd[1], input + off, n - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        close(pfd[1]);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static const char *find_tool(const char *name, char *buf, size_t n)
{
    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    char *copy = strdup(path);
    if (!copy) return NULL;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ":", &save); tok;
         tok = strtok_r(NULL, ":", &save)) {
        snprintf(buf, n, "%s/%s", tok, name);
        if (access(buf, X_OK) == 0) { free(copy); return buf; }
    }
    free(copy);
    return NULL;
}

static int run_tool(const char *name, char *const extra[])
{
    char path[512];
    if (!find_tool(name, path, sizeof(path))) {
        fprintf(stderr, "installer: не найдена утилита %s\n", name);
        return -1;
    }
    char *argv[32];
    int n = 0;
    argv[n++] = path;
    for (int i = 0; extra[i] && n < 31; i++) argv[n++] = extra[i];
    argv[n] = NULL;
    return run_argv(argv);
}

static int capture(const char *cmd, char *const argv[], char *out, size_t n)
{
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], 1);
        close(pfd[1]);
        execvp(cmd, argv);
        _exit(127);
    }
    close(pfd[1]);
    size_t off = 0;
    while (off + 1 < n) {
        ssize_t r = read(pfd[0], out + off, n - off - 1);
        if (r <= 0) break;
        off += (size_t)r;
    }
    out[off] = 0;
    close(pfd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    while (off && (out[off - 1] == '\n' || out[off - 1] == '\r'))
        out[--off] = 0;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

static void mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, mode);
        *p = '/';
    }
    mkdir(tmp, mode);
}

static int write_file(const char *path, const char *data, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    size_t n = strlen(data), off = 0;
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w <= 0) { close(fd); return -1; }
        off += (size_t)w;
    }
    if (fchmod(fd, mode) != 0) { /* best effort */ }
    if (fsync(fd) != 0) { /* best effort */ }
    close(fd);
    return 0;
}

static char *read_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        if (len + 1024 + 1 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(fd); return NULL; }
            buf = nb;
        }
        ssize_t r = read(fd, buf + len, cap - len - 1);
        if (r < 0) { if (errno == EINTR) continue; free(buf); close(fd); return NULL; }
        if (r == 0) break;
        len += (size_t)r;
    }
    buf[len] = 0;
    close(fd);
    return buf;
}

static int copy_file(const char *src, const char *dst, const struct stat *st)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 07777);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    int ok = 0;
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) { ok = 1; break; }
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) { if (errno == EINTR) continue; break; }
            off += w;
        }
        if (off != r) break;
    }
    if (ok) {
        fchmod(out, st->st_mode & 07777);
        if (fchown(out, st->st_uid, st->st_gid) != 0) { /* best effort */ }
        struct timespec ts[2] = { st->st_atim, st->st_mtim };
        futimens(out, ts);
    }
    close(out);
    close(in);
    return ok ? 0 : -1;
}

/* Копирует дерево без внешних утилит: сохраняет права, владельцев,
 * символические ссылки и время изменения. */
static int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) != 0) return -1;

    if (S_ISLNK(st.st_mode)) {
        char link[1024];
        ssize_t n = readlink(src, link, sizeof(link) - 1);
        if (n < 0) return -1;
        link[n] = 0;
        unlink(dst);
        if (symlink(link, dst) != 0) return -1;
        if (lchown(dst, st.st_uid, st.st_gid) != 0) { /* best effort */ }
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        utimensat(AT_FDCWD, dst, ts, AT_SYMLINK_NOFOLLOW);
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, 0700);
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *de;
        int rc = 0;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s[1024], t[1024];
            snprintf(s, sizeof(s), "%s/%s", src, de->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, de->d_name);
            if (copy_tree(s, t) != 0) rc = -1;
        }
        closedir(d);
        chmod(dst, st.st_mode & 07777);
        if (chown(dst, st.st_uid, st.st_gid) != 0) { /* best effort */ }
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        utimensat(AT_FDCWD, dst, ts, 0);
        return rc;
    }
    if (S_ISREG(st.st_mode))
        return copy_file(src, dst, &st);
    return 0; /* устройства, сокеты, fifo не копируем */
}

static int path_mounted(const char *dev)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char line[1024];
    size_t dl = strlen(dev);
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        if (!strncmp(line, dev, dl)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static unsigned long long disk_size(const char *disk)
{
    int fd = open(disk, O_RDONLY);
    if (fd >= 0) {
        unsigned long long bytes = 0;
        if (ioctl(fd, BLKGETSIZE64, &bytes) == 0) { close(fd); return bytes; }
        close(fd);
    }
    const char *base = strrchr(disk, '/');
    if (!base) return 0;
    char path[256];
    snprintf(path, sizeof(path), "/sys/block/%s/size", base + 1);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long long sectors = 0;
    if (fscanf(f, "%llu", &sectors) != 1) sectors = 0;
    fclose(f);
    return sectors * 512ULL;
}

static void list_disks(void)
{
    DIR *d = opendir("/sys/block");
    if (!d) { perror("installer: /sys/block"); return; }
    struct dirent *de;
    printf("  Доступные диски:\n");
    while ((de = readdir(d))) {
        const char *n = de->d_name;
        if (!strncmp(n, "loop", 4) || !strncmp(n, "ram", 3) ||
            !strncmp(n, "sr", 2) || !strncmp(n, "dm-", 3) ||
            !strncmp(n, "zram", 4) || !strncmp(n, "fd", 2))
            continue;
        char path[320];
        snprintf(path, sizeof(path), "/dev/%.255s", n);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISBLK(st.st_mode)) continue;
        unsigned long long sz = disk_size(path);
        char model[64] = "";
        char mpath[320];
        snprintf(mpath, sizeof(mpath), "/sys/block/%.255s/device/model", n);
        FILE *mf = fopen(mpath, "r");
        if (mf) {
            if (fgets(model, sizeof(model), mf)) model[strcspn(model, "\n")] = 0;
            fclose(mf);
        }
        printf("    %-14s %6.1f GiB  %s\n", path, (double)sz / (1024.0 * 1024.0 * 1024.0), model);
    }
    closedir(d);
}

static void part_path(const char *disk, int num, size_t n, char *out)
{
    size_t dl = strlen(disk);
    if (dl && isdigit((unsigned char)disk[dl - 1]))
        snprintf(out, n, "%sp%d", disk, num);
    else
        snprintf(out, n, "%s%d", disk, num);
}

static int reload_partitions(const char *disk)
{
    int fd = open(disk, O_RDONLY);
    int rc = -1;
    if (fd >= 0) {
        rc = ioctl(fd, BLKRRPART, 0);
        close(fd);
    }
    sync();
    sleep(1);
    return rc;
}

static int create_partitions(const char *disk)
{
    char rootp[160], espp[160];
    part_path(disk, 2, sizeof(rootp), rootp);
    part_path(disk, 1, sizeof(espp), espp);

    char wipe[512];
    if (find_tool("wipefs", wipe, sizeof(wipe))) {
        char *av[] = { wipe, "-a", (char *)disk, NULL };
        (void)run_argv(av);
    }

    char ptool[512];
    if (find_tool("parted", ptool, sizeof(ptool))) {
        printf("  >>> разметка %s (parted, %s)\n", disk,
               g_uefi ? "UEFI" : "BIOS");
        if (g_uefi) {
            char *av[] = { ptool, "-s", (char *)disk,
                "mklabel", "gpt",
                "mkpart", "ESP", "fat32", "1MiB", "513MiB",
                "set", "1", "esp", "on",
                "mkpart", "root", "ext4", "513MiB", "100%", NULL };
            if (run_argv(av) != 0) { die("parted не создал разделы"); return -1; }
        } else {
            char *av[] = { ptool, "-s", (char *)disk,
                "mklabel", "gpt",
                "mkpart", "bios_grub", "1MiB", "2MiB",
                "set", "1", "bios_grub", "on",
                "mkpart", "root", "ext4", "2MiB", "100%", NULL };
            if (run_argv(av) != 0) { die("parted не создал разделы"); return -1; }
        }
    } else if (find_tool("sfdisk", ptool, sizeof(ptool))) {
        printf("  >>> разметка %s (sfdisk, %s)\n", disk,
               g_uefi ? "UEFI" : "BIOS");
        const char *script = g_uefi
            ? "label: gpt\n"
              "start=2048, size=1048576, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name=ESP\n"
              "start=1050624, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name=root\n"
            : "label: gpt\n"
              "start=2048, size=2048, type=21686148-6449-6E6F-744E-656564454649, name=bios_grub\n"
              "start=4096, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name=root\n";
        char *av[] = { ptool, "-q", (char *)disk, NULL };
        if (run_input(av, script) != 0) { die("sfdisk не создал разделы"); return -1; }
    } else {
        die("нужен parted или sfdisk");
        return -1;
    }
    reload_partitions(disk);

    struct stat st;
    if (stat(rootp, &st) != 0) {
        die("корневой раздел не появился (проверьте dmesg)");
        return -1;
    }
    return 0;
}

static int format_filesystems(const char *disk)
{
    char rootp[160], espp[160];
    part_path(disk, 2, sizeof(rootp), rootp);
    part_path(disk, 1, sizeof(espp), espp);

    if (g_uefi) {
        printf("  >>> mkfs.vfat %s\n", espp);
        char *av[] = { "mkfs.vfat", "-F", "32", "-n", "YAITSO-ESP", espp, NULL };
        if (run_tool("mkfs.vfat", av + 1) != 0) {
            die("mkfs.vfat не удался (нужен пакет dosfstools в образе)");
            return -1;
        }
    }

    printf("  >>> mkfs.ext4 %s\n", rootp);
    char *av[] = { "-F", "-L", "yaitso-root", rootp, NULL };
    if (run_tool("mkfs.ext4", av) != 0) {
        die("mkfs.ext4 не удался (нужен e2fsprogs в образе)");
        return -1;
    }
    return 0;
}

static int mount_target(const char *disk)
{
    char rootp[160], espp[160];
    part_path(disk, 2, sizeof(rootp), rootp);
    part_path(disk, 1, sizeof(espp), espp);

    mkdir_p(TARGET, 0755);
    if (mount(rootp, TARGET, "ext4", 0, NULL) != 0) {
        die("не удалось смонтировать корневой раздел");
        return -1;
    }
    g_root_mounted = 1;

    if (g_uefi) {
        char efi[512];
        snprintf(efi, sizeof(efi), "%s/boot/efi", TARGET);
        mkdir_p(efi, 0755);
        if (mount(espp, efi, "vfat", 0, NULL) != 0) {
            die("не удалось смонтировать EFI-раздел");
            return -1;
        }
        g_esp_mounted = 1;
    }
    return 0;
}

static int copy_system(void)
{
    static const char *dirs[] = { "bin", "sbin", "lib", "lib64", "usr",
                                  "etc", "var", "opt", "root", "home",
                                  "repo", NULL };
    static const char *empty[] = { "proc", "sys", "dev", "run", "tmp",
                                   "mnt", "media", "boot", "srv", NULL };

    for (int i = 0; empty[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", TARGET, empty[i]);
        mkdir_p(path, strcmp(empty[i], "tmp") == 0 ? 01777 : 0755);
    }

    for (int i = 0; dirs[i]; i++) {
        char src[512], dst[512];
        snprintf(src, sizeof(src), "/%s", dirs[i]);
        snprintf(dst, sizeof(dst), "%s/%s", TARGET, dirs[i]);
        struct stat st;
        if (lstat(src, &st) != 0) continue;
        printf("  >>> копирование /%s\n", dirs[i]);
        fflush(stdout);
        if (copy_tree(src, dst) != 0)
            fprintf(stderr, "installer: предупреждение: ошибки при копировании /%s\n",
                    dirs[i]);
    }
    return 0;
}

static void get_uuid(const char *dev, char *out, size_t n, int *have)
{
    out[0] = 0;
    *have = 0;
    char tool[512];
    if (find_tool("blkid", tool, sizeof(tool))) {
        char *av[] = { tool, "-s", "UUID", "-o", "value", (char *)dev, NULL };
        if (capture(tool, av, out, n) == 0 && out[0]) { *have = 1; return; }
    }
}

static int write_target_config(const char *hostname, const char *user,
                               int gui)
{
    char path[512];
    char hn[128];
    snprintf(hn, sizeof(hn), "%s\n", hostname);
    snprintf(path, sizeof(path), "%s/etc/hostname", TARGET);
    write_file(path, hn, 0644);

    char hosts[512];
    snprintf(hosts, sizeof(hosts),
             "127.0.0.1 localhost\n127.0.1.1 %s\n::1 localhost\n", hostname);
    snprintf(path, sizeof(path), "%s/etc/hosts", TARGET);
    write_file(path, hosts, 0644);

    snprintf(path, sizeof(path), "%s/etc/yaitso", TARGET);
    mkdir_p(path, 0755);
    snprintf(path, sizeof(path), "%s/etc/yaitso/session", TARGET);
    write_file(path, gui ? "desktop\n" : "text\n", 0644);

    /* live-образ входит автоматически; установленная система — нет. */
    snprintf(path, sizeof(path), "%s/etc/yaitso/autologin", TARGET);
    unlink(path);

    /* resolv.conf в live указывает на 1.1.1.1; оставляем как разумный
     * стартовый вариант, но копируем живой, если он есть. */
    snprintf(path, sizeof(path), "%s/etc/resolv.conf", TARGET);
    write_file(path, "nameserver 1.1.1.1\nnameserver 8.8.8.8\n", 0644);

    snprintf(path, sizeof(path), "%s/etc/os-release", TARGET);
    write_file(path,
        "NAME=\"Yaitso OS\"\n"
        "PRETTY_NAME=\"Yaitso OS " INSTALLER_VERSION "\"\n"
        "ID=yaitso\n"
        "VERSION=\"" INSTALLER_VERSION "\"\n"
        "VERSION_ID=\"" INSTALLER_VERSION "\"\n",
        0644);

    (void)user;
    return 0;
}

static int write_fstab(const char *disk)
{
    char rootp[160], espp[160];
    part_path(disk, 2, sizeof(rootp), rootp);
    part_path(disk, 1, sizeof(espp), espp);
    char ruuid[128], euuid[128];
    int rh = 0, eh = 0;
    get_uuid(rootp, ruuid, sizeof(ruuid), &rh);
    if (g_uefi) get_uuid(espp, euuid, sizeof(euuid), &eh);

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "# /etc/fstab: Yaitso OS\n"
        "# <file system> <mount point> <type> <options> <dump> <pass>\n"
        "%s%s / ext4 defaults,noatime 0 1\n"
        "proc /proc proc defaults 0 0\n"
        "sysfs /sys sysfs defaults 0 0\n"
        "devtmpfs /dev devtmpfs defaults 0 0\n"
        "tmpfs /tmp tmpfs mode=1777 0 0\n",
        rh ? "UUID=" : "", rh ? ruuid : rootp);
    if (g_uefi) {
        char line[256];
        snprintf(line, sizeof(line),
                 "%s%s /boot/efi vfat umask=0077 0 2\n",
                 eh ? "UUID=" : "", eh ? euuid : espp);
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/etc/fstab", TARGET);
    return write_file(path, buf, 0644);
}

static int copy_kernel(void)
{
    char dst[512];
    snprintf(dst, sizeof(dst), "%s/boot/vmlinuz", TARGET);
    FILE *f = fopen("/boot/vmlinuz", "rb");
    if (!f) {
        fprintf(stderr,
            "installer: предупреждение: /boot/vmlinuz отсутствует в live-системе\n");
        return -1;
    }
    fclose(f);

    snprintf(dst, sizeof(dst), "%s/boot", TARGET);
    mkdir_p(dst, 0755);

    snprintf(dst, sizeof(dst), "%s/boot/vmlinuz", TARGET);
    struct stat st;
    if (stat("/boot/vmlinuz", &st) != 0 || copy_file("/boot/vmlinuz", dst, &st) != 0) {
        fprintf(stderr, "installer: не удалось скопировать ядро\n");
        return -1;
    }
    snprintf(dst, sizeof(dst), "%s/boot/initramfs.gz", TARGET);
    if (stat("/boot/initramfs.gz", &st) == 0)
        copy_file("/boot/initramfs.gz", dst, &st);
    return 0;
}

static int write_grub_cfg(const char *disk)
{
    char rootp[160];
    part_path(disk, 2, sizeof(rootp), rootp);

    /* Корень указываем устройством, а не UUID: минимальный initramfs
     * установленной системы монтирует его до запуска /sbin/init и не
     * содержит blkid для разбора UUID. /etc/fstab при этом остаётся
     * на UUID — это важно для нормальной работы системы. */
    char cfg[1024];
    snprintf(cfg, sizeof(cfg),
        "set default=0\n"
        "set timeout=5\n\n"
        "menuentry \"Yaitso OS " INSTALLER_VERSION "\" {\n"
        "    linux /boot/vmlinuz root=%s rw quiet "
        "console=tty0 console=ttyS0,115200n8\n"
        "    initrd /boot/initramfs.gz\n"
        "}\n\n"
        "menuentry \"Yaitso OS (verbose)\" {\n"
        "    linux /boot/vmlinuz root=%s rw "
        "console=tty0 console=ttyS0,115200n8\n"
        "    initrd /boot/initramfs.gz\n"
        "}\n",
        rootp, rootp);

    char path[512];
    snprintf(path, sizeof(path), "%s/boot/grub", TARGET);
    mkdir_p(path, 0755);
    snprintf(path, sizeof(path), "%s/boot/grub/grub.cfg", TARGET);
    return write_file(path, cfg, 0644);
}

/* Генерирует SHA-512 crypt хеш с случайной солью. */
static int make_hash(const char *pass, char *out, size_t n)
{
    static const char alpha[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char rnd[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t got = read(fd, rnd, sizeof(rnd));
    close(fd);
    if (got != (ssize_t)sizeof(rnd)) return -1;
    char salt[32] = "$6$";
    for (size_t i = 0; i < 16; i++)
        salt[3 + i] = alpha[rnd[i] % 64];
    salt[19] = 0;
    char *h = crypt(pass, salt);
    if (!h || !h[0]) return -1;
    snprintf(out, n, "%s", h);
    return 0;
}

static int set_shadow_field(const char *user, const char *hash_or_null)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/etc/shadow", TARGET);
    char *data = read_file(path);
    if (!data) {
        fprintf(stderr, "installer: нет %s\n", path);
        return -1;
    }
    /* Ёмкость считаем до strtok_r: он заменяет разделители на NUL, и
     * strlen(data) после разбора уже не равен длине файла. */
    size_t cap = strlen(data) + 256;
    char *out = calloc(1, cap);
    if (!out) { free(data); return -1; }
    size_t olen = 0;
    int changed = 0;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char copy[1024];
        snprintf(copy, sizeof(copy), "%s", line);
        char *fields[10] = {0};
        int nf = 0;
        char *p = copy;
        while (nf < 10) {
            fields[nf++] = p;
            char *c = strchr(p, ':');
            if (!c) break;
            *c = 0;
            p = c + 1;
        }
        if (nf >= 2 && !strcmp(fields[0], user)) {
            const char *newhash = hash_or_null ? hash_or_null : "!";
            size_t need = strlen(fields[0]) + strlen(newhash) + 2;
            for (int i = 2; i < nf; i++) need += strlen(fields[i]) + 1;
            if (olen + need + 2 > cap) { free(data); free(out); return -1; }
            olen += (size_t)snprintf(out + olen, cap - olen,
                                     "%s:%s", fields[0], newhash);
            for (int i = 2; i < nf; i++)
                olen += (size_t)snprintf(out + olen, cap - olen,
                                         ":%s", fields[i]);
            out[olen++] = '\n';
            changed = 1;
        } else {
            size_t need = strlen(line) + 1;
            if (olen + need + 1 > cap) { free(data); free(out); return -1; }
            olen += (size_t)snprintf(out + olen, cap - olen, "%s\n", line);
        }
    }
    free(data);
    if (!changed) {
        fprintf(stderr, "installer: пользователь %s не найден в shadow\n", user);
        free(out);
        return -1;
    }
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s/etc/shadow.tmp", TARGET);
    if (write_file(tmp, out, 0600) != 0) { free(out); return -1; }
    free(out);
    snprintf(path, sizeof(path), "%s/etc/shadow", TARGET);
    if (rename(tmp, path) != 0) return -1;
    chmod(path, 0600);
    return 0;
}

static int setup_user(const char *user, const char *pass)
{
    struct stat st;
    char path[512];
    snprintf(path, sizeof(path), "%s/usr/sbin/useradd", TARGET);
    if (stat(path, &st) != 0) {
        die("в установленной системе нет useradd");
        return -1;
    }
    char *argv[] = { "/usr/sbin/useradd", "-m", "-G",
                     "wheel,video,input,render,audio",
                     "-s", "/bin/sh", (char *)user, NULL };
    printf("  >>> создание пользователя %s\n", user);
    if (run_chroot(TARGET, "/usr/sbin/useradd", argv, NULL) != 0) {
        die("useradd завершился с ошибкой");
        return -1;
    }

    char hash[256];
    if (make_hash(pass, hash, sizeof(hash)) != 0) {
        die("не удалось создать хеш пароля");
        return -1;
    }
    if (set_shadow_field(user, hash) != 0) return -1;

    /* Пароль root в live-образе публично известен. В установленной системе
     * root блокируется, вход — только через созданного пользователя. */
    if (set_shadow_field("root", NULL) != 0)
        fprintf(stderr, "installer: предупреждение: не удалось заблокировать root\n");
    return 0;
}

static void bind_mounts(void)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/dev", TARGET);
    if (mount("/dev", p, NULL, MS_BIND | MS_REC, NULL) == 0) g_bind_dev = 1;
    snprintf(p, sizeof(p), "%s/proc", TARGET);
    if (mount("/proc", p, NULL, MS_BIND | MS_REC, NULL) == 0) g_bind_proc = 1;
    snprintf(p, sizeof(p), "%s/sys", TARGET);
    if (mount("/sys", p, NULL, MS_BIND | MS_REC, NULL) == 0) g_bind_sys = 1;
    snprintf(p, sizeof(p), "%s/run", TARGET);
    if (mount("/run", p, NULL, MS_BIND | MS_REC, NULL) == 0) g_bind_run = 1;
}

/* В live-системе sudo/su/passwd должны быть setuid root — без этого после
 * установки не работают sudo и смена пароля. Копирование файлов может
 * потерять setuid-бит, поэтому выставляем его явно. */
static void fix_privileged(void)
{
    static const char *const bins[] = {
        "/usr/bin/sudo", "/usr/bin/su", "/usr/bin/passwd",
        "/bin/su", "/usr/bin/sg", "/usr/bin/chsh", "/usr/bin/chfn",
        NULL
    };
    for (int i = 0; bins[i]; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s%s", TARGET, bins[i]);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (st.st_uid != 0 && chown(p, 0, 0) != 0) {
            fprintf(stderr, "installer: предупреждение: не удалось сменить "
                    "владельца %s\n", bins[i]);
            continue;
        }
        if (chmod(p, 04755) != 0)
            fprintf(stderr, "installer: предупреждение: не удалось выставить "
                    "setuid на %s\n", bins[i]);
    }
}

/* grub-install в разных дистрибутивах бывает и ELF-бинарником, и shell-
 * скриптом. Бинарник нужно запускать напрямую: `bash /usr/sbin/grub-install`
 * пытается разобрать его как текст и падает с «cannot execute binary file».
 * Скрипт запускаем через bash.real, потому что /bin/sh в target — yaitso. */
static int target_is_script(const char *prog)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", TARGET, prog);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char magic[2] = {0, 0};
    ssize_t n = read(fd, magic, sizeof(magic));
    close(fd);
    return n == 2 && magic[0] == '#' && magic[1] == '!';
}

static int run_target(const char *prog, char *const args[])
{
    if (!target_is_script(prog))
        return run_chroot(TARGET, prog, args, NULL);
    if (access(TARGET "/usr/bin/bash.real", X_OK) != 0)
        return run_chroot(TARGET, prog, args, NULL);

    char *av[16];
    int n = 0;
    av[n++] = "/usr/bin/bash.real";
    av[n++] = (char *)prog;
    for (int i = 0; args[i] && n < 15; i++) av[n++] = args[i];
    av[n] = NULL;
    return run_chroot(TARGET, "/usr/bin/bash.real", av, NULL);
}

static int install_bootloader(const char *disk)
{
    int rc = -1;

    if (g_uefi) {
        printf("  >>> grub-install (UEFI)\n");
        char *args[] = {
            "--target=x86_64-efi", "--efi-directory=/boot/efi",
            "--bootloader-id=Yaitso", "--recheck",
            "--no-nvram", NULL };
        rc = run_target("/usr/sbin/grub-install", args);
        if (rc != 0) {
            /* Резерв: готовый EFI-загрузчик из live-образа. */
            char src[512], dst[512];
            snprintf(src, sizeof(src), "%s/usr/lib/yaitso/BOOTX64.EFI", TARGET);
            if (access(src, R_OK) == 0) {
                snprintf(dst, sizeof(dst), "%s/boot/efi/EFI/BOOT", TARGET);
                mkdir_p(dst, 0755);
                strncat(dst, "/BOOTX64.EFI", sizeof(dst) - strlen(dst) - 1);
                struct stat st;
                if (stat(src, &st) == 0 && copy_file(src, dst, &st) == 0) {
                    printf("  >>> установлен резервный EFI-загрузчик\n");
                    rc = 0;
                }
            }
        }
    } else {
        printf("  >>> grub-install (BIOS)\n");
        char *args[] = { "--target=i386-pc", "--recheck", (char *)disk, NULL };
        rc = run_target("/usr/sbin/grub-install", args);
    }

    if (rc != 0)
        fprintf(stderr, "installer: предупреждение: загрузчик не установлен\n");
    return rc;
}

static void cleanup(void)
{
    char p[512];
    if (g_bind_run) { snprintf(p, sizeof(p), "%s/run", TARGET); umount2(p, MNT_DETACH); }
    if (g_bind_sys) { snprintf(p, sizeof(p), "%s/sys", TARGET); umount2(p, MNT_DETACH); }
    if (g_bind_proc) { snprintf(p, sizeof(p), "%s/proc", TARGET); umount2(p, MNT_DETACH); }
    if (g_bind_dev) { snprintf(p, sizeof(p), "%s/dev", TARGET); umount2(p, MNT_DETACH); }
    if (g_esp_mounted) {
        snprintf(p, sizeof(p), "%s/boot/efi", TARGET);
        umount2(p, MNT_DETACH);
    }
    if (g_root_mounted) umount2(TARGET, MNT_DETACH);
}

static int ask_yes_no(const char *prompt, int def)
{
    char buf[32];
    printf("%s [%s]: ", prompt, def ? "Y/n" : "y/N");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return def;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!buf[0]) return def;
    return buf[0] == 'y' || buf[0] == 'Y';
}

static void read_line(char *buf, size_t n)
{
    if (!fgets(buf, n, stdin)) { buf[0] = 0; return; }
    buf[strcspn(buf, "\r\n")] = 0;
}

static void read_password(char *buf, size_t n)
{
    struct termios old, new;
    if (tcgetattr(0, &old) != 0) { read_line(buf, n); return; }
    new = old;
    new.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(0, TCSAFLUSH, &new);
    read_line(buf, n);
    tcsetattr(0, TCSAFLUSH, &old);
    printf("\n");
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

static int valid_hostname(const char *s)
{
    if (!s[0] || strlen(s) > 63) return 0;
    for (const char *p = s; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '.')) return 0;
    return 1;
}

static void usage(void)
{
    printf("Yaitso OS installer %s\n"
           "usage: yaitso-install [options]\n"
           "  --list                 показать диски и выйти\n"
           "  --disk /dev/vda        диск для установки\n"
           "  --hostname NAME        имя компьютера (по умолчанию yaitso)\n"
           "  --user NAME            имя пользователя\n"
           "  --password PASS        пароль пользователя\n"
           "  --gui | --no-gui       графическая или текстовая сессия\n"
           "  --efi | --bios         тип загрузки (по умолчанию автоопределение)\n"
           "  --yes, -y              не задавать вопросов\n"
           "  --force                разрешить установку на смонтированный диск\n",
           INSTALLER_VERSION);
}

int main(int argc, char **argv)
{
    const char *disk = NULL;
    const char *hostname = NULL;
    const char *user = NULL;
    const char *pass = NULL;
    int want_gui = -1;
    int efi_set = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(); return 0;
        } else if (!strcmp(argv[i], "--list")) {
            print_header(); list_disks(); return 0;
        } else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            disk = argv[++i];
        } else if (!strcmp(argv[i], "--hostname") && i + 1 < argc) {
            hostname = argv[++i];
        } else if (!strcmp(argv[i], "--user") && i + 1 < argc) {
            user = argv[++i];
        } else if (!strcmp(argv[i], "--password") && i + 1 < argc) {
            pass = argv[++i];
        } else if (!strcmp(argv[i], "--gui")) {
            want_gui = 1;
        } else if (!strcmp(argv[i], "--no-gui") || !strcmp(argv[i], "--text")) {
            want_gui = 0;
        } else if (!strcmp(argv[i], "--efi") || !strcmp(argv[i], "--uefi")) {
            g_uefi = 1;
            efi_set = 1;
        } else if (!strcmp(argv[i], "--bios")) {
            g_uefi = 0;
            efi_set = 1;
        } else if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) {
            g_yes = 1;
        } else if (!strcmp(argv[i], "--force")) {
            g_force = 1;
        } else {
            fprintf(stderr, "installer: неизвестный аргумент %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    print_header();

    if (!efi_set)
        g_uefi = access("/sys/firmware/efi", F_OK) == 0;

    char diskbuf[128] = "";
    if (!disk) {
        list_disks();
        printf("\n  Диск для установки [/dev/vda]: ");
        fflush(stdout);
        read_line(diskbuf, sizeof(diskbuf));
        if (!diskbuf[0]) snprintf(diskbuf, sizeof(diskbuf), "/dev/vda");
        disk = diskbuf;
    }

    struct stat dst;
    if (stat(disk, &dst) != 0 || !S_ISBLK(dst.st_mode)) {
        fprintf(stderr, "installer: %s не является блочным устройством\n", disk);
        return 1;
    }
    if (strncmp("/dev/", disk, 5) != 0) {
        fprintf(stderr, "installer: ожидается путь /dev/...\n");
        return 1;
    }
    unsigned long long bytes = disk_size(disk);
    if (bytes > 0 && bytes < MIN_DISK_BYTES) {
        fprintf(stderr, "installer: диск меньше 4 GiB\n");
        return 1;
    }
    if (!g_force && path_mounted(disk)) {
        fprintf(stderr, "installer: на %s есть смонтированные разделы\n", disk);
        return 1;
    }

    char hostbuf[64] = "yaitso";
    if (!hostname) {
        printf("  Hostname [yaitso]: ");
        fflush(stdout);
        read_line(hostbuf, sizeof(hostbuf));
        if (!hostbuf[0]) snprintf(hostbuf, sizeof(hostbuf), "yaitso");
        hostname = hostbuf;
    }
    if (!valid_hostname(hostname)) {
        fprintf(stderr, "installer: недопустимый hostname\n");
        return 1;
    }

    char userbuf[64] = "";
    if (!user) {
        do {
            printf("  Имя пользователя: ");
            fflush(stdout);
            read_line(userbuf, sizeof(userbuf));
        } while (!valid_username(userbuf));
        user = userbuf;
    }
    if (!valid_username(user)) {
        fprintf(stderr, "installer: недопустимое имя пользователя\n");
        return 1;
    }

    char passbuf[128] = "";
    if (!pass) {
        char confirm[128] = "";
        do {
            printf("  Пароль пользователя: ");
            fflush(stdout);
            read_password(passbuf, sizeof(passbuf));
            printf("  Повторите пароль: ");
            fflush(stdout);
            read_password(confirm, sizeof(confirm));
            if (!passbuf[0] || strchr(passbuf, ':')) {
                printf("  Пароль не может быть пустым или содержать ':'\n");
                continue;
            }
            if (strcmp(passbuf, confirm) != 0) {
                printf("  Пароли не совпадают, попробуйте снова\n");
                passbuf[0] = 0;
            }
        } while (!passbuf[0]);
        pass = passbuf;
    }
    if (!pass[0] || strchr(pass, ':')) {
        fprintf(stderr, "installer: недопустимый пароль\n");
        return 1;
    }

    if (want_gui < 0) {
        printf("\n  Установить графическую среду? (Wayland: ycomp/yterm/ybar)\n");
        want_gui = ask_yes_no("  Установить?", 1);
    }

    printf("\n  Установка Yaitso OS %s\n", INSTALLER_VERSION);
    printf("  ----------------------------------------\n");
    printf("  Диск:      %s (%.1f GiB)\n", disk,
           (double)bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  Загрузка:  %s\n", g_uefi ? "UEFI" : "BIOS");
    printf("  Hostname:  %s\n", hostname);
    printf("  Пользователь: %s\n", user);
    printf("  Сессия:    %s\n", want_gui ? "графическая" : "текстовая");
    printf("  ----------------------------------------\n");
    printf("  ВНИМАНИЕ: все данные на %s будут уничтожены!\n\n", disk);

    if (!g_yes) {
        printf("  Для продолжения введите YES: ");
        fflush(stdout);
        char answer[32] = "";
        read_line(answer, sizeof(answer));
        if (strcmp(answer, "YES") != 0) {
            printf("  Отменено.\n");
            return 1;
        }
    }

    if (create_partitions(disk) != 0) goto fail;
    if (format_filesystems(disk) != 0) goto fail;
    if (mount_target(disk) != 0) goto fail;

    printf("  >>> копирование системы (это может занять несколько минут)\n");
    if (copy_system() != 0) goto fail;

    if (write_target_config(hostname, user, want_gui) != 0) {
        die("не удалось записать конфигурацию");
        goto fail;
    }
    if (write_fstab(disk) != 0) {
        die("не удалось записать /etc/fstab");
        goto fail;
    }
    copy_kernel();
    if (write_grub_cfg(disk) != 0) {
        die("не удалось записать grub.cfg");
        goto fail;
    }

    bind_mounts();
    if (setup_user(user, pass) != 0) goto fail;
    fix_privileged();

    install_bootloader(disk);

    sync();
    cleanup();
    printf("\n  Установка завершена.\n");
    printf("  Извлеките установочный носитель и перезагрузитесь.\n\n");
    if (g_yes || ask_yes_no("  Перезагрузиться сейчас?", 0)) {
        sync();
        reboot(RB_AUTOBOOT);
    }
    return 0;

fail:
    cleanup();
    fprintf(stderr, "\ninstaller: установка прервана.\n");
    return 1;
}
