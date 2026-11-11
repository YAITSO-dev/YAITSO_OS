#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <crypt.h>
#include <pwd.h>
#include <shadow.h>
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
    struct spwd *sp = getspnam(user);
    const char *hash = sp ? sp->sp_pwdp : NULL;
    if (!hash || !*hash || hash[0] == '!' || hash[0] == '*') return -1;
    char *h = crypt(pass, hash);
    return h && strcmp(h, hash) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    int login_shell = 0;
    const char *user = "root";
    int ai = 1;
    if (ai < argc && (!strcmp(argv[ai], "-") || !strcmp(argv[ai], "-l"))) {
        login_shell = 1;
        ai++;
    }
    if (ai < argc) user = argv[ai++];
    if (ai < argc) {
        fprintf(stderr, "usage: su [-] [user]\n");
        return 2;
    }

    struct passwd *pw = getpwnam(user);
    if (!pw) { fprintf(stderr, "su: %s не найден\n", user); return 1; }

    if (getuid() != 0 && pw->pw_uid != getuid()) {
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
    const char *shell = (pw->pw_shell && *pw->pw_shell) ? pw->pw_shell
                                                        : "/bin/sh";
    setenv("SHELL", shell, 1);

    if (login_shell) {
        if (chdir(pw->pw_dir) != 0) chdir("/");
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);
    }

    execl(shell, shell, (char *)NULL);
    perror("su: exec");
    return 1;
}
