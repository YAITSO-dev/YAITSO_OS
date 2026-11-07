#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <errno.h>

static int filter_file(const char *path, const char *name)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return -1; }

    char line[1024];
    int removed = 0;
    while (fgets(line, sizeof(line), f)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", line);
        char *colon = strchr(buf, ':');
        if (colon) *colon = 0;
        if (!strcmp(buf, name)) { removed = 1; continue; }
        fputs(line, o);
    }
    fclose(f);
    fclose(o);
    if (removed) rename(tmp, path);
    else unlink(tmp);
    return removed;
}

int main(int argc, char **argv)
{
    int remove_home = 0;
    const char *name = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) remove_home = 1;
        else if (!name) name = argv[i];
    }

    if (!name) { fprintf(stderr, "usage: userdel [-r] <user>\n"); return 2; }

    struct passwd *pw = getpwnam(name);
    if (!pw) { fprintf(stderr, "userdel: %s не найден\n", name); return 1; }

    char home[256];
    snprintf(home, sizeof(home), "%s", pw->pw_dir);

    if (filter_file("/etc/passwd", name) < 0) { perror("/etc/passwd"); return 1; }
    filter_file("/etc/group", name);
    filter_file("/etc/shadow", name);

    if (remove_home && home[0] == '/') {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", home);
        system(cmd);
    }

    printf("userdel: %s удалён\n", name);
    return 0;
}
