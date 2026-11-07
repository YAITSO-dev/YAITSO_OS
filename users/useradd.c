#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

static int valid_name(const char *s)
{
    if (!s || !*s) return 0;
    if (!islower((unsigned char)s[0]) && s[0] != '_') return 0;
    for (const char *p = s; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) &&
            *p != '_' && *p != '-') return 0;
    return 1;
}

static int append_line(const char *path, const char *line)
{
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fprintf(f, "%s\n", line);
    fclose(f);
    return 0;
}

static int read_last_uid(void)
{
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return 1000;
    char line[1024];
    int max = 999;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        char *p = colon + 1;
        char *colon2 = strchr(p, ':');
        if (!colon2) continue;
        *colon2 = 0;
        int uid = atoi(p);
        if (uid > max) max = uid;
    }
    fclose(f);
    return max + 1;
}

static int create_home(const char *user, int uid, int gid)
{
    char home[256];
    snprintf(home, sizeof(home), "/home/%s", user);
    if (mkdir(home, 0755) != 0 && errno != EEXIST) return -1;
    if (chown(home, (uid_t)uid, (gid_t)gid) != 0) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    int create_m = 0;
    const char *group = NULL;
    const char *name = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m")) create_m = 1;
        else if (!strcmp(argv[i], "-G") && i + 1 < argc) group = argv[++i];
        else if (!name) name = argv[i];
    }

    if (!valid_name(name)) {
        fprintf(stderr, "useradd: невалидное имя\n");
        return 1;
    }

    struct passwd *exists = getpwnam(name);
    if (exists) { fprintf(stderr, "useradd: %s уже существует\n", name); return 1; }

    int uid = read_last_uid();
    int gid = uid;

    char pw_line[512];
    snprintf(pw_line, sizeof(pw_line), "%s:x:%d:%d::/home/%s:/bin/sh",
             name, uid, gid, name);
    if (append_line("/etc/passwd", pw_line) != 0) {
        perror("/etc/passwd");
        return 1;
    }

    char grp_line[256];
    snprintf(grp_line, sizeof(grp_line), "%s:x:%d:", name, gid);
    append_line("/etc/group", grp_line);

    char sh_line[256];
    snprintf(sh_line, sizeof(sh_line), "%s:!:::::::", name);
    append_line("/etc/shadow", sh_line);

    if (group) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "usermod -aG %s %s", group, name);
        system(cmd);
    }

    if (create_m) create_home(name, uid, gid);

    printf("useradd: %s создан (uid %d, gid %d)\n", name, uid, gid);
    return 0;
}
