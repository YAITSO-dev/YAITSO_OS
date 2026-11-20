#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>

static void set_if_empty(const char *key, const char *val)
{
    const char *cur = getenv(key);
    if (!cur || !*cur) setenv(key, val, 1);
}

int main(void) {
    int uid = getuid();
    char xdg_runtime[256];
    snprintf(xdg_runtime, sizeof(xdg_runtime), "/run/user/%d", uid);
    mkdir("/run/user", 0755);
    if (mkdir(xdg_runtime, 0700) != 0 && errno != EEXIST) {
        /* /run/user может быть недоступен для записи (старая система или
         * нестандартный init). Используем приватный каталог в /tmp. */
        snprintf(xdg_runtime, sizeof(xdg_runtime), "/tmp/yaitso-runtime-%d", uid);
        mkdir(xdg_runtime, 0700);
    }
    chmod(xdg_runtime, 0700);
    if (chown(xdg_runtime, (uid_t)uid, (gid_t)getgid()) != 0) {
        /* best effort: каталог уже мог принадлежать пользователю */
    }

    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw && pw->pw_dir && *pw->pw_dir && !getenv("HOME"))
        setenv("HOME", pw->pw_dir, 1);

    setenv("XDG_RUNTIME_DIR", xdg_runtime, 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "yaitso", 1);
    setenv("XDG_SESSION_DESKTOP", "yaitso", 1);
    set_if_empty("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    set_if_empty("XDG_CONFIG_HOME", "/etc/xdg");
    set_if_empty("GDK_BACKEND", "wayland");
    set_if_empty("QT_QPA_PLATFORM", "wayland");
    set_if_empty("MOZ_ENABLE_WAYLAND", "1");
    set_if_empty("SDL_VIDEODRIVER", "wayland");

    // Программный рендеринг и обход сложных проверок DRM.
    set_if_empty("WLR_RENDERER", "pixman");
    set_if_empty("WLR_NO_HARDWARE_CURSORS", "1");
    set_if_empty("LIBSEAT_BACKEND", "noop");
    set_if_empty("WLR_BACKENDS", "drm");
    /* Атомарный DRM обязателен: на legacy-пути (WLR_DRM_NO_ATOMIC=1)
     * wlr_scene_output_commit() не выполняет page-flip, и экран остаётся
     * чёрным при живом композиторе и клиентах. */
    unsetenv("WLR_DRM_NO_ATOMIC");
    set_if_empty("WLR_DRM_NO_MODIFIERS", "1");

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

    /* Журнал ycomp. /var/log доступен только root, поэтому у обычного
     * пользователя пишем в его XDG_RUNTIME_DIR. */
    char logpath[400];
    snprintf(logpath, sizeof(logpath), "%s/ycomp.log", xdg_runtime);
    int logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd < 0 && uid == 0) {
        snprintf(logpath, sizeof(logpath), "/var/log/ycomp.log");
        logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO) close(logfd);
    }

    execl("/usr/bin/ycomp", "ycomp", (char *)NULL);
    return 1;
}
