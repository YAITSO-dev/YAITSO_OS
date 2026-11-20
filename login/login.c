#define _GNU_SOURCE
#include "login.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>

#define BANS_DIR       "/var/lib/yaitso/bans"
#define MAX_TRIES      3
#define LOCK_SECONDS   300

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

static int nologin_exists(void) { return access("/etc/nologin", F_OK) == 0; }

static void show_nologin(void)
{
    FILE *f = fopen("/etc/nologin", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) fputs(line, stdout);
    fclose(f);
}

/* Допустимые имена: без '/', без "..", без управляющих символов. */
static int valid_user(const char *user)
{
    if (!user || !*user) return 0;
    size_t len = strlen(user);
    if (len >= 64) return 0;
    if (!strcmp(user, ".") || !strcmp(user, "..")) return 0;
    for (const unsigned char *p = (const unsigned char *)user; *p; p++) {
        if (*p < 0x21 || *p > 0x7e) return 0;
        if (*p == '/') return 0;
    }
    return 1;
}

static void ban_path(const char *user, char *out, size_t n)
{
    if (!valid_user(user)) { if (n) out[0] = 0; return; }
    snprintf(out, n, "%s/%s", BANS_DIR, user);
}

static int check_ban(const char *user)
{
    char path[256];
    ban_path(user, path, sizeof(path));
    if (!path[0]) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int cnt = 0;
    long when = 0;
    if (fscanf(f, "%d %ld", &cnt, &when) != 2) {
        fclose(f);
        unlink(path);
        return 0;
    }
    fclose(f);
    time_t now = time(NULL);
    if (now - when >= LOCK_SECONDS) { unlink(path); return 0; }
    if (cnt >= MAX_TRIES) return 1;
    return 0;
}

static void record_fail(const char *user)
{
    mkdir("/var/lib", 0755);
    mkdir("/var/lib/yaitso", 0755);
    mkdir(BANS_DIR, 0700);

    char path[256];
    ban_path(user, path, sizeof(path));
    if (!path[0]) return;

    int cnt = 0;
    FILE *f = fopen(path, "r");
    if (f) { if (fscanf(f, "%d", &cnt) != 1) cnt = 0; fclose(f); }
    cnt++;

    f = fopen(path, "w");
    if (!f) return;
    chmod(path, 0600);
    fprintf(f, "%d %ld\n", cnt, (long)time(NULL));
    fclose(f);
}

static void clear_fail(const char *user)
{
    char path[256];
    ban_path(user, path, sizeof(path));
    if (path[0]) unlink(path);
}

static void read_line(char *buf, size_t n)
{
    size_t i = 0;
    while (i + 1 < n) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        buf[i++] = c;
    }
    buf[i] = 0;
}

static void read_pass(char *buf, size_t n)
{
    struct termios old, new;
    tcgetattr(0, &old);
    new = old;
    new.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(0, TCSAFLUSH, &new);
    read_line(buf, n);
    tcsetattr(0, TCSAFLUSH, &old);
    ssize_t wr = write(1, "\n", 1);
    (void)wr;
}

static int tty_ok(void)
{
    struct stat st;
    if (fstat(0, &st) < 0) return 0;
    if (!S_ISCHR(st.st_mode)) return 0;
    if (st.st_uid != 0) return 0;
    return 1;
}

static void reset_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

/* Автоматический вход: -f user, -a user или /etc/yaitso/autologin.
 * Используется live-образом; установленная система этот файл не содержит. */
static int autologin_user(char *out, size_t n)
{
    /* Автовход из файла — только на tty1. Иначе все getty (tty1-tty4)
     * одновременно стартуют графическую сессию, делят DRM и порождают
     * лавину перезапусков. Явные -f/-a остаются без ограничения. */
    const char *tty = ttyname(STDIN_FILENO);
    if (!tty || strcmp(tty, "/dev/tty1") != 0) return 0;

    const char *file = "/etc/yaitso/autologin";
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    char line[64] = "";
    int ok = 0;
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && valid_user(p)) {
            snprintf(out, n, "%s", p);
            ok = 1;
        }
    }
    fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    if (!tty_ok()) {
        fprintf(stderr, "login: tty owner check failed\n");
        return 1;
    }

    reset_signals();

    char host[64] = "yaitso";
    gethostname(host, sizeof(host) - 1);

    char auto_name[64] = "";
    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "-a") ||
             !strcmp(argv[i], "--autologin")) && i + 1 < argc) {
            if (valid_user(argv[i + 1]))
                snprintf(auto_name, sizeof(auto_name), "%s", argv[++i]);
        }
    }
    if (!auto_name[0])
        autologin_user(auto_name, sizeof(auto_name));

    struct passwd *auto_pw = auto_name[0] ? getpwnam(auto_name) : NULL;
    if (auto_pw && check_ban(auto_pw->pw_name))
        auto_pw = NULL;
    if (auto_pw && nologin_exists() && strcmp(auto_pw->pw_name, "root") != 0)
        auto_pw = NULL;

    char user[64] = "";
    int authenticated = 0;

    if (auto_pw) {
        snprintf(user, sizeof(user), "%s", auto_pw->pw_name);
        authenticated = 1;
    }

    for (int attempt = 0; !authenticated && attempt < MAX_TRIES; attempt++) {
        printf("%s login: ", host);
        fflush(stdout);
        read_line(user, sizeof(user));
        if (!user[0]) continue;
        if (!valid_user(user) || !getpwnam(user)) {
            printf("login: unknown user\n");
            sleep(1);
            continue;
        }

        if (check_ban(user)) {
            printf("login: account locked for %d seconds\n", LOCK_SECONDS);
            sleep(2);
            return 1;
        }

        if (nologin_exists() && strcmp(user, "root") != 0) {
            show_nologin();
            sleep(2);
            return 1;
        }

        printf("Password: ");
        fflush(stdout);
        char pass[128];
        read_pass(pass, sizeof(pass));

        if (crypt_check(user, pass) != 0) {
            record_fail(user);
            printf("login: incorrect\n");
            sleep(2);
            if (check_ban(user)) {
                printf("login: too many attempts, locked for %d seconds\n",
                       LOCK_SECONDS);
                sleep(2);
                return 1;
            }
            continue;
        }

        clear_fail(user);
        authenticated = 1;
    }

    if (!authenticated) return 1;

    struct passwd *pw = getpwnam(user);
    if (!pw) return 1;

    char *shell = (pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/sh";

    if (setgid(pw->pw_gid) != 0) return 1;
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) return 1;
    if (setuid(pw->pw_uid) != 0) return 1;

    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("SHELL", shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);
    setenv("TERM", "linux", 0);

    int chdir_rc = chdir(pw->pw_dir);
    if (chdir_rc != 0) chdir_rc = chdir("/");
    (void)chdir_rc;
    umask(022);

    if (is_desktop_session()) {
        execl("/usr/bin/start-desktop", "start-desktop", (char *)NULL);
    }
    execl(shell, shell, (char *)NULL);
    perror("login: exec");
    return 1;
}
