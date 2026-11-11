#define _GNU_SOURCE
#include "ycomp.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static void ipc_send_all(int fd, const char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

static void ipc_handle(struct ycomp_server *s, int fd, char *line)
{
    line[strcspn(line, "\r\n")] = 0;
    if (!*line) { ipc_send_all(fd, "err\n"); return; }

    if (!strcmp(line, "list")) {
        struct ycomp_view *v;
        char buf[512];
        wl_list_for_each(v, &s->views, link) {
            if (!v->mapped) continue;
            const char *title = v->xdg_toplevel->title ?
                                v->xdg_toplevel->title : "";
            const char *app   = v->xdg_toplevel->app_id ?
                                v->xdg_toplevel->app_id : "";
            snprintf(buf, sizeof(buf), "%d\t%s\t%s\t%d\n", v->id, app, title,
                     v->id == s->focused_id ? 1 : 0);
            ipc_send_all(fd, buf);
        }
        return;
    }
    if (!strncmp(line, "focus ", 6)) {
        int id = atoi(line + 6);
        ycomp_focus_view(s, id);
        ipc_send_all(fd, "ok\n");
        return;
    }
    if (!strncmp(line, "close ", 6)) {
        int id = atoi(line + 6);
        ycomp_close_view(s, id);
        ipc_send_all(fd, "ok\n");
        return;
    }
    if (!strcmp(line, "status")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ok\twindows=%d\n", ycomp_count_views(s));
        ipc_send_all(fd, buf);
        return;
    }
    if (!strcmp(line, "terminal")) {
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execlp(ycomp_cfg_terminal(), ycomp_cfg_terminal(), (char *)NULL);
            _exit(1);
        }
        ipc_send_all(fd, "ok\n");
        return;
    }
    if (!strcmp(line, "launcher")) {
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execlp(ycomp_cfg_launcher(), ycomp_cfg_launcher(), (char *)NULL);
            _exit(1);
        }
        ipc_send_all(fd, "ok\n");
        return;
    }
    if (!strcmp(line, "exit")) {
        ipc_send_all(fd, "ok\n");
        wl_display_terminate(s->display);
        return;
    }
    ipc_send_all(fd, "err\n");
}

static int peer_allowed(int fd)
{
    struct ucred cred;
    socklen_t clen = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen) != 0)
        return 0;
    if (cred.uid == 0) return 1;
    if (cred.uid == getuid()) return 1;
    return 0;
}

static int ipc_accept(int fd, uint32_t mask, void *data)
{
    (void)mask;
    struct ycomp_server *s = data;
    int c = accept(fd, NULL, NULL);
    if (c < 0) return 0;
    if (!peer_allowed(c)) { close(c); return 0; }
    char buf[256];
    ssize_t n = read(c, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        ipc_handle(s, c, buf);
    }
    close(c);
    return 0;
}

int ycomp_ipc_init(struct ycomp_server *s)
{
    unlink(YCOMP_SOCKET);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", YCOMP_SOCKET);

    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    chmod(YCOMP_SOCKET, 0600);

    if (listen(fd, 16) < 0) { close(fd); return -1; }

    s->ipc_fd = fd;
    s->ipc_source = wl_event_loop_add_fd(s->event_loop, fd,
                                         WL_EVENT_READABLE,
                                         ipc_accept, s);
    return s->ipc_source ? 0 : -1;
}

void ycomp_ipc_fini(struct ycomp_server *s)
{
    if (s->ipc_source) wl_event_source_remove(s->ipc_source);
    if (s->ipc_fd >= 0) close(s->ipc_fd);
    unlink(YCOMP_SOCKET);
}
