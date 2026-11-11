#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>

/* Пути переопределяются при сборке для тестов (-DWIFI_BASE=... и т.п.). */
#ifndef WIFI_BASE
#define WIFI_BASE "/etc/yaitso"
#endif
#ifndef WIFI_DIR
#define WIFI_DIR WIFI_BASE "/wifi"
#endif
#ifndef WPA_CONF
#define WPA_CONF "/etc/wpa_supplicant.conf"
#endif

/* Только для КОНСТАНТНЫХ команд. Пользовательские данные сюда не передавать.
 * Не используем system(): в образе glibc передаёт шеллу "sh -c -- cmd",
 * а шелл yaitso такой вид не понимает и всегда возвращает 127. */
static int run_const(const char *cmd)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/*
 * Запуск программы без шелла: аргументы не интерпретируются.
 * stdout (если out_path не NULL) пишется в файл, stderr уходит в /dev/null.
 */
static int run_exec(char *const args[], const char *out_path)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (out_path) {
            int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
            if (fd < 0) _exit(126);
            dup2(fd, 1);
            close(fd);
        }
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 2); close(nul); }
        execvp(args[0], args);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* SSID: 1..32 байта, без '/', '\\', '"', управляющих символов, не начинается с '.'.
 * Апостроф допустим: теперь аргументы не проходят через шелл. */
static int valid_ssid(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > 32) return 0;
    if (s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return 0;
        if (c == '/' || c == '\\' || c == '"') return 0;
    }
    return 1;
}

/* Пароль WPA-PSK: пусто (открытая сеть) или 8..63 символа без управляющих. */
static int valid_pass(const char *s)
{
    size_t n = strlen(s);
    if (n == 0) return 1;
    if (n < 8 || n > 63) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return 0;
    }
    return 1;
}

static void ensure_dir(void)
{
    mkdir(WIFI_BASE, 0755);
    mkdir(WIFI_DIR, 0700);
}

static int write_profile(const char *ssid, const char *pass)
{
    ensure_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.conf", WIFI_DIR, ssid);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { perror(path); return -1; }
    dprintf(fd, "ssid = %s\n", ssid);
    if (pass[0]) dprintf(fd, "psk = %s\n", pass);
    close(fd);
    return 0;
}

static int cmd_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("scanning...\n");
    int r = run_const("iwlist scan 2>/dev/null | grep -E 'ESSID|Signal'");
    if (r != 0) {
        printf("(нет iwlist, показать через iw)\n");
        run_const("iw dev 2>/dev/null");
    }
    return 0;
}

static int cmd_list(int argc, char **argv)
{
    (void)argc; (void)argv;
    DIR *d = opendir(WIFI_DIR);
    if (!d) { printf("(нет профилей)\n"); return 0; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l < 5) continue;
        if (strcmp(de->d_name + l - 5, ".conf")) continue;
        printf("  %.*s\n", (int)(l - 5), de->d_name);
    }
    closedir(d);
    return 0;
}

/* argv здесь уже без "ywifi connect": argv[0] = ssid, argv[1] = пароль (необязательно). */
static int cmd_connect(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "usage: ywifi connect <ssid> [pass]\n"); return 2; }
    const char *ssid = argv[0];
    const char *pass = (argc > 1) ? argv[1] : "";

    if (!valid_ssid(ssid)) {
        fprintf(stderr, "ywifi: недопустимое имя сети (SSID)\n");
        return 2;
    }
    if (!valid_pass(pass)) {
        fprintf(stderr, "ywifi: пароль должен быть 8–63 символа без управляющих\n");
        return 2;
    }
    if (write_profile(ssid, pass) != 0) return 1;

    printf(">>> подключение к %s...\n", ssid);
    if (pass[0]) {
        char *args[] = { "wpa_passphrase", (char *)ssid, (char *)pass, NULL };
        if (run_exec(args, WPA_CONF) != 0) {
            fprintf(stderr, "ywifi: wpa_passphrase failed\n");
            return 1;
        }
    } else {
        int fd = open(WPA_CONF, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            dprintf(fd, "network={\n  ssid=\"%s\"\n  key_mgmt=NONE\n}\n", ssid);
            close(fd);
        }
    }
    run_const("wpa_supplicant -B -i wlan0 -c " WPA_CONF " 2>/dev/null");
    run_const("dhclient wlan0 2>/dev/null || udhcpc -i wlan0 2>/dev/null");
    printf(">>> %s подключён\n", ssid);
    return 0;
}

static int cmd_disconnect(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_const("killall wpa_supplicant 2>/dev/null");
    run_const("ip link set wlan0 down 2>/dev/null");
    printf(">>> отключено\n");
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_const("ip addr show wlan0 2>/dev/null | grep -E 'state|inet'");
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "ywifi 0.3.2 — WiFi-менеджер Yaitso OS\n"
        "  ywifi scan                 — поиск сетей\n"
        "  ywifi list                 — сохранённые профили\n"
        "  ywifi connect <ssid> [pwd] — подключиться\n"
        "  ywifi disconnect           — отключиться\n"
        "  ywifi status               — состояние\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "scan"))       return cmd_scan(argc - 2, argv + 2);
    if (!strcmp(cmd, "list"))       return cmd_list(argc - 2, argv + 2);
    if (!strcmp(cmd, "connect"))    return cmd_connect(argc - 2, argv + 2);
    if (!strcmp(cmd, "disconnect")) return cmd_disconnect(argc - 2, argv + 2);
    if (!strcmp(cmd, "status"))     return cmd_status(argc - 2, argv + 2);
    usage();
    return 2;
}
