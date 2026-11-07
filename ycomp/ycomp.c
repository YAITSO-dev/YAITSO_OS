#include "ycomp.h"
#include <wlr/backend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

static struct ycomp_server g_server;

static void on_signal(int sig)
{
    (void)sig;
    if (g_server.display) wl_display_terminate(g_server.display);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime) {
        runtime = "/run/user/0";
        setenv("XDG_RUNTIME_DIR", runtime, 1);
    }
    mkdir(runtime, 0700);

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);
    signal(SIGPIPE, SIG_IGN);

    memset(&g_server, 0, sizeof(g_server));
    g_server.next_id = 1;
    g_server.focused_id = -1;
    g_server.ipc_fd = -1;

    if (ycomp_server_init(&g_server) != 0) {
        fprintf(stderr, "ycomp: server init failed\n");
        return 1;
    }
    ycomp_config_load(&g_server);
    ycomp_decorations_init(&g_server);

    if (ycomp_ipc_init(&g_server) != 0) {
        fprintf(stderr, "ycomp: ipc init failed\n");
        return 1;
    }

    const char *sock = wl_display_add_socket_auto(g_server.display);
    if (!sock) {
        fprintf(stderr, "ycomp: cannot add socket\n");
        return 1;
    }
    fprintf(stderr, "ycomp: WAYLAND_DISPLAY=%s\n", sock);

    if (!wlr_backend_start(g_server.backend)) {
        fprintf(stderr, "ycomp: backend start failed\n");
        return 1;
    }

    setenv("WAYLAND_DISPLAY", sock, 1);

    wl_display_run(g_server.display);

    ycomp_ipc_fini(&g_server);
    wl_display_destroy_clients(g_server.display);
    wl_display_destroy(g_server.display);
    return 0;
}
