#include "yterm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

int yt_pty_spawn(struct yt_term *t, char *const argv[], int *master_fd)
{
    int master = -1, slave = -1;
    struct winsize ws = {
        .ws_row = (unsigned short)t->rows,
        .ws_col = (unsigned short)t->cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };

    if (openpty(&master, &slave, NULL, NULL, &ws) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(master); close(slave); return -1; }
    if (pid == 0) {
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);

        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        execvp(argv[0], argv);
        _exit(127);
    }
    close(slave);
    *master_fd = master;
    return 0;
}

void yt_pty_resize(int master_fd, int cols, int rows)
{
    if (master_fd < 0) return;
    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
    };
    ioctl(master_fd, TIOCSWINSZ, &ws);
    pid_t pg = tcgetpgrp(master_fd);
    if (pg > 0) kill(-pg, SIGWINCH);
}
