#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <crypt.h>
#include <pwd.h>
#include <shadow.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

extern char *crypt(const char *, const char *);

static void read_pass(char *buf, size_t n)
{
    struct termios old, new;
    if (tcgetattr(0, &old) != 0) {
        if (!fgets(buf, (int)n, stdin)) buf[0] = 0;
    } else {
        new = old;
        new.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(0, TCSAFLUSH, &new);
        if (!fgets(buf, (int)n, stdin)) buf[0] = 0;
        tcsetattr(0, TCSAFLUSH, &old);
        write(1, "\n", 1);
    }
    buf[strcspn(buf, "\r\n")] = 0;
}

static int check_pass(const char *user, const char *pass)
{
    struct spwd *sp = getspnam(user);
    const char *hash = sp ? sp->sp_pwdp : NULL;
    if (!hash || !*hash || hash[0] == '!' || hash[0] == '*') return -1;
    char *h = crypt(pass, hash);
    return h && strcmp(h, hash) == 0 ? 0 : -1;
}

/*
 * Атомарно переписывает /etc/shadow: mkstemp в /etc, права 0600,
 * fchmod/fchown, затем rename. Так не остаётся частично записанного
 * файла и не теряются права доступа.
 */
static int update_shadow(const char *user, const char *hash)
{
    int in = open("/etc/shadow", O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    FILE *f = fdopen(in, "r");
    if (!f) { close(in); return -1; }

    char tmp[] = "/etc/shadow.XXXXXX";
    int out = mkstemp(tmp);
    if (out < 0) { fclose(f); return -1; }
    fchmod(out, 0600);
    if (fchown(out, 0, 0) != 0) { /* не фатально */ }

    FILE *o = fdopen(out, "w");
    if (!o) { close(out); unlink(tmp); fclose(f); return -1; }

    char line[1024];
    int done = 0;
    long today = (long)(time(NULL) / 86400);
    while (fgets(line, sizeof(line), f)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", line);
        char *colon = strchr(buf, ':');
        if (colon) *colon = 0;
        if (!strcmp(buf, user)) {
            fprintf(o, "%s:%s:%ld:0:99999:7:::\n", user, hash, today);
            done = 1;
        } else {
            fputs(line, o);
        }
    }
    if (!done) fprintf(o, "%s:%s:%ld:0:99999:7:::\n", user, hash, today);

    if (fflush(o) != 0 || fsync(fileno(o)) != 0) {
        fclose(o);
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(o);
    fclose(f);

    if (rename(tmp, "/etc/shadow") != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: passwd [user]\n");
        return 2;
    }

    struct passwd *self = getpwuid(getuid());
    const char *user = (argc == 2) ? argv[1] : (self ? self->pw_name : NULL);
    if (!user) { fprintf(stderr, "passwd: cannot determine user\n"); return 1; }

    struct passwd *pw = getpwnam(user);
    if (!pw) { fprintf(stderr, "passwd: %s не найден\n", user); return 1; }

    int is_root = (getuid() == 0);
    if (!is_root && pw->pw_uid != getuid()) {
        fprintf(stderr, "passwd: недостаточно прав\n");
        return 1;
    }

    if (!is_root) {
        char old[128];
        printf("Current password: ");
        fflush(stdout);
        read_pass(old, sizeof(old));
        if (check_pass(user, old) != 0) {
            fprintf(stderr, "passwd: неверный пароль\n");
            return 1;
        }
    }

    char pass1[128], pass2[128];
    printf("New password: ");
    fflush(stdout);
    read_pass(pass1, sizeof(pass1));
    printf("Retype new password: ");
    fflush(stdout);
    read_pass(pass2, sizeof(pass2));

    if (strcmp(pass1, pass2) != 0) {
        fprintf(stderr, "passwd: пароли не совпадают\n");
        return 1;
    }
    if (!pass1[0]) {
        fprintf(stderr, "passwd: пустой пароль недопустим\n");
        return 1;
    }

    char salt[64];
    snprintf(salt, sizeof(salt), "$6$%08lx%08lx", (unsigned long)time(NULL),
             (unsigned long)getpid());
    char *hash = crypt(pass1, salt);
    if (!hash) { perror("crypt"); return 1; }

    if (update_shadow(user, hash) != 0) {
        fprintf(stderr, "passwd: не удалось обновить /etc/shadow\n");
        return 1;
    }

    printf("passwd: пароль обновлён для %s\n", user);
    return 0;
}
