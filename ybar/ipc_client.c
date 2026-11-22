#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ybar.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>

#define YBAR_IPC_TIMEOUT_MS 500

static int connect_timeout(int fd, struct sockaddr *addr, socklen_t alen, int ms)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    int r = connect(fd, addr, alen);
    if (r == 0) {
        fcntl(fd, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) return -1;

    struct pollfd p = { fd, POLLOUT, 0 };
    r = poll(&p, 1, ms);
    if (r <= 0) return -1;

    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0) return -1;
    if (err != 0) { errno = err; return -1; }

    fcntl(fd, F_SETFL, flags);
    return 0;
}

int ybar_ipc_send(const char *cmd, char *reply, size_t rn)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt)
        snprintf(a.sun_path, sizeof(a.sun_path), "%s/ycomp.sock", rt);
    else
        snprintf(a.sun_path, sizeof(a.sun_path), "/run/ycomp.sock");

    if (connect_timeout(fd, (struct sockaddr *)&a, sizeof(a),
                        YBAR_IPC_TIMEOUT_MS) != 0) {
        close(fd);
        return -1;
    }

    struct timeval tv = {0, YBAR_IPC_TIMEOUT_MS * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    size_t wn = strlen(cmd), sent = 0;
    while (sent < wn) {
        ssize_t w = write(fd, cmd + sent, wn - sent);
        if (w <= 0) { close(fd); return -1; }
        sent += (size_t)w;
    }
    /* Сервер читает команду построчно; без перевода строки он ждал бы
     * таймаут, а панель выглядела бы зависшей. */
    if (wn == 0 || cmd[wn - 1] != '\n') {
        char nl = '\n';
        if (write(fd, &nl, 1) != 1) { close(fd); return -1; }
    }
    ssize_t n = read(fd, reply, rn - 1);
    close(fd);
    if (n < 0) return -1;
    reply[n] = 0;
    return 0;
}

int ybar_ipc_list(struct ybar_state *s)
{
    s->nwin = 0;
    s->focus_id = -1;
    char buf[8192];
    if (ybar_ipc_send("list", buf, sizeof(buf)) != 0) return -1;
    char *p = buf;
    while (*p && s->nwin < YBAR_MAX_WIN) {
        char *eol = strchr(p, '\n');
        if (!eol) break;
        *eol = 0;
        char *fields[5] = {0};
        int nf = 0;
        char *q = p;
        while (nf < 5) {
            fields[nf++] = q;
            char *tab = strchr(q, '\t');
            if (!tab) break;
            *tab = 0;
            q = tab + 1;
        }
        if (nf >= 3) {
            int idx = s->nwin;
            int id = atoi(fields[0]);
            s->win_id[idx] = id;
            snprintf(s->wins[idx], sizeof(s->wins[idx]), "%s",
                     fields[2][0] ? fields[2] : fields[1]);
            s->win_hidden[idx] = (nf >= 5) ? atoi(fields[4]) : 0;
            if (nf >= 4 && atoi(fields[3]) == 1) s->focus_id = id;
            s->nwin++;
        }
        p = eol + 1;
    }
    return 0;
}
