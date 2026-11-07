#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <crypt.h>
#include <pwd.h>
#include <errno.h>
#include <time.h>

extern char *crypt(const char *, const char *);

static void read_pass(char *buf, size_t n)
{
    struct termios old, new;
    tcgetattr(0, &old);
    new = old;
    new.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(0, TCSAFLUSH, &new);
    if (!fgets(buf, (int)n, stdin)) buf[0] = 0;
    tcsetattr(0, TCSAFLUSH, &old);
    write(1, "\n", 1);
    buf[strcspn(buf, "\r\n")] = 0;
}

static int update_shadow(const char *user, const char *hash)
{
    FILE *f = fopen("/etc/shadow", "r");
    if (!f) return -1;
    char tmp[256] = "/etc/shadow.tmp";
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return -1; }

    char line[1024];
    int done = 0;
    while (fgets(line, sizeof(line), f)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", line);
        char *colon = strchr(buf, ':');
        if (colon) *colon = 0;
        if (!strcmp(buf, user)) {
            fprintf(o, "%s:%s:19000:0:99999:7:::\n", user, hash);
            done = 1;
        } else {
            fputs(line, o);
        }
    }
    if (!done) fprintf(o, "%s:%s:19000:0:99999:7:::\n", user, hash);
    fclose(f);
    fclose(o);
    rename(tmp, "/etc/shadow");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: passwd [user]\n");
        return 2;
    }

    const char *user = argv[1];
    struct passwd *pw = getpwnam(user);
    if (!pw) { fprintf(stderr, "passwd: %s не найден\n", user); return 1; }

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

    char salt[32];
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
