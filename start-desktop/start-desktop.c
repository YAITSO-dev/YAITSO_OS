#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

int main(void) {
    int uid = getuid();
    char xdg_runtime[256];
    snprintf(xdg_runtime, sizeof(xdg_runtime), "/run/user/%d", uid);
    mkdir("/run/user", 0755);
    mkdir(xdg_runtime, 0700);

    setenv("XDG_RUNTIME_DIR", xdg_runtime, 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "yaitso", 1);
    setenv("XDG_SESSION_DESKTOP", "yaitso", 1);

    // ГЛАВНОЕ: программный рендеринг и обход всех сложных проверок DRM
    setenv("WLR_RENDERER", "pixman", 1);
    setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
    setenv("LIBSEAT_BACKEND", "noop", 1);
    setenv("WLR_BACKENDS", "drm", 1);
    /* Атомарный DRM обязателен: на legacy-пути (WLR_DRM_NO_ATOMIC=1)
     * wlr_scene_output_commit() не выполняет page-flip, и экран остаётся
     * чёрным при живом композиторе и клиентах. */
    unsetenv("WLR_DRM_NO_ATOMIC");
    setenv("WLR_DRM_NO_MODIFIERS", "1", 1);

    // Ищем любой /dev/dri/cardN (не только card0), ждём до 10 секунд.
    char card[64] = "";
    for (int attempt = 0; attempt < 10 && !card[0]; attempt++) {
        for (int i = 0; i < 10; i++) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/dri/card%d", i);
            if (access(path, F_OK) == 0) {
                snprintf(card, sizeof(card), "%s", path);
                break;
            }
        }
        if (!card[0]) sleep(1);
    }
    if (!card[0]) {
        fprintf(stderr, "start-desktop: no /dev/dri/card* found\n");
        return 1;
    }
    setenv("WLR_DRM_DEVICES", card, 1);

    /* ycomp создаёт сокет уже после fork, поэтому дочерние процессы ждут его,
     * а не спят фиксированное время. */
    char sock_path[320];
    snprintf(sock_path, sizeof(sock_path), "%s/wayland-0", xdg_runtime);

    pid_t bar = fork();
    if (bar == 0) {
        for (int i = 0; i < 100 && access(sock_path, F_OK) != 0; i++)
            usleep(100000);
        if (access(sock_path, F_OK) != 0) {
            fprintf(stderr, "start-desktop: no Wayland socket %s\n", sock_path);
            _exit(1);
        }
        execl("/usr/bin/ybar", "ybar", (char *)NULL);
        _exit(1);
    }

    pid_t term = fork();
    if (term == 0) {
        for (int i = 0; i < 100 && access(sock_path, F_OK) != 0; i++)
            usleep(100000);
        if (access(sock_path, F_OK) != 0) _exit(1);
        usleep(300000);
        execl("/usr/bin/yterm", "yterm", (char *)NULL);
        _exit(1);
    }

    /* init запускает GUI на tty1, поэтому stderr ycomp иначе теряется.
     * Журнал помогает диагностировать проблемы с DRM/вводом. */
    int logfd = open("/var/log/ycomp.log",
                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO) close(logfd);
    }

    execl("/usr/bin/ycomp", "ycomp", (char *)NULL);
    return 1;
}
