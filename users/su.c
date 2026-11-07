#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <crypt.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

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

static int check_pass(const char *user, const char *pass)
{
    struct passwd *pw = getpwnam(user);
    if (!pw) return -1;
    char *h = crypt(pass, pw->pw_passwd);
    return h && strcmp(h, pw->pw_passwd) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *user = (argc > 1) ? argv[1] : "root";

    struct passwd *pw = getpwnam(user);
    if (!pw) { fprintf(stderr, "su: %s не найден\n", user); return 1; }

    if (getuid() != 0) {
        char pass[128];
        fprintf(stderr, "Password: ");
        fflush(stderr);
        read_pass(pass, sizeof(pass));
        if (check_pass(user, pass) != 0) {
            fprintf(stderr, "su: неверный пароль\n");
            return 1;
        }
    }

    if (setgid(pw->pw_gid) != 0) return 1;
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) return 1;
    if (setuid(pw->pw_uid) != 0) return 1;

    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("SHELL", pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh", 1);

    const char *shell = getenv("SHELL");
    execl(shell, shell, (char *)NULL);
    perror("su: exec");
    return 1;
}
