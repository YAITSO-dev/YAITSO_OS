#include "ycomp.h"
#include <wlr/backend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

static struct ycomp_server g_server;

static int g_evdev_rescans;
static struct wl_event_source *g_evdev_timer;

static int evdev_rescan(void *data)
{
    (void)data;
    /* Устройства ввода могут появиться чуть позже backend'а (например,
     * psmouse регистрирует мышь асинхронно). Несколько повторных сканов
     * подхватывают их, уже открытые узлы пропускаются. */
    ycomp_evdev_init(&g_server);
    if (--g_evdev_rescans > 0)
        wl_event_source_timer_update(g_evdev_timer, 500);
    return 0;
}

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

    /* Сначала поднимаем backend и выход, чтобы к моменту появления сокета
     * output_layout уже знал размер экрана для configure окон и панели. */
    if (!wlr_backend_start(g_server.backend)) {
        fprintf(stderr, "ycomp: backend start failed\n");
        return 1;
    }

    /* При WLR_BACKENDS=drm backend не создаёт устройств ввода и libinput
     * недоступен без udev. Читаем evdev напрямую. YCOMP_EVDEV=0 отключает
     * это, YCOMP_EVDEV=1 — принудительно включает. */
    const char *backends = getenv("WLR_BACKENDS");
    const char *evdev_env = getenv("YCOMP_EVDEV");
    int use_evdev = 0;
    if (evdev_env && *evdev_env)
        use_evdev = strcmp(evdev_env, "0") != 0;
    else if (backends && strstr(backends, "drm") &&
             !strstr(backends, "libinput"))
        use_evdev = 1;
    if (use_evdev) {
        ycomp_evdev_init(&g_server);
        g_evdev_rescans = 4;
        g_evdev_timer = wl_event_loop_add_timer(g_server.event_loop,
                                                evdev_rescan, NULL);
        if (g_evdev_timer)
            wl_event_source_timer_update(g_evdev_timer, 500);
    }
    /* start-desktop задаёт WAYLAND_DISPLAY заранее, чтобы ybar/yterm знали,
     * к какому сокету подключаться. Если имя занято — берём первое свободное. */
    const char *wanted = getenv("WAYLAND_DISPLAY");
    const char *sock = NULL;
    if (wanted && *wanted && !strchr(wanted, '/')) {
        if (wl_display_add_socket(g_server.display, wanted) == 0)
            sock = wanted;
    }
    if (!sock)
        sock = wl_display_add_socket_auto(g_server.display);
    if (!sock) {
        fprintf(stderr, "ycomp: cannot add socket\n");
        return 1;
    }
    fprintf(stderr, "ycomp: WAYLAND_DISPLAY=%s\n", sock);
    setenv("WAYLAND_DISPLAY", sock, 1);

    wl_display_run(g_server.display);

    ycomp_ipc_fini(&g_server);
    wl_display_destroy_clients(g_server.display);
    wl_display_destroy(g_server.display);
    return 0;
}
