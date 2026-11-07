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
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "yaitso", 1);
    setenv("XDG_SESSION_DESKTOP", "yaitso", 1);

    // ГЛАВНОЕ: программный рендеринг и обход всех сложных проверок DRM
    setenv("WLR_RENDERER", "pixman", 1);
    setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
    setenv("LIBSEAT_BACKEND", "noop", 1);
    setenv("WLR_BACKENDS", "drm", 1);
    setenv("WLR_DRM_DEVICES", "/dev/dri/card0", 1);
    setenv("WLR_DRM_NO_ATOMIC", "1", 1);
    setenv("WLR_DRM_NO_MODIFIERS", "1", 1);

    // Ждем card0 до 10 секунд
    int found = 0;
    for (int i = 0; i < 10; i++) {
        if (access("/dev/dri/card0", F_OK) == 0) { found = 1; break; }
        sleep(1);
    }
    if (!found) {
        fprintf(stderr, "start-desktop: /dev/dri/card0 not found\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        sleep(2);
        execl("/usr/bin/ybar", "ybar", (char *)NULL);
        _exit(1);
    }

    execl("/usr/bin/ycomp", "ycomp", (char *)NULL);
    return 1;
}
