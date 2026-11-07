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

static int user_in_wheel(const char *user)
{
    struct group *gr = getgrnam("wheel");
    if (!gr) return 0;
    for (char **m = gr->gr_mem; m && *m; m++)
        if (!strcmp(*m, user)) return 1;
    return 0;
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
    if (argc < 2) { fprintf(stderr, "usage: sudo <command> [args]\n"); return 2; }

    struct passwd *pw = getpwuid(getuid());
    if (!pw) { fprintf(stderr, "sudo: cannot determine user\n"); return 1; }

    int is_root = (getuid() == 0);
    int is_wheel = user_in_wheel(pw->pw_name);

    if (!is_root) {
        if (!is_wheel) {
            fprintf(stderr, "sudo: %s не в группе wheel\n", pw->pw_name);
            return 1;
        }
        char pass[128];
        fprintf(stderr, "[sudo] password for %s: ", pw->pw_name);
        fflush(stderr);
        read_pass(pass, sizeof(pass));
        if (check_pass(pw->pw_name, pass) != 0) {
            fprintf(stderr, "sudo: неверный пароль\n");
            return 1;
        }
    }

    if (setgid(0) != 0) return 1;
    if (setuid(0) != 0) return 1;

    execvp(argv[1], argv + 1);
    perror("sudo: exec");
    return 1;
}
