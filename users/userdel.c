#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ftw.h>

static int valid_name(const char *s)
{
    if (!s || !*s || strlen(s) >= 32) return 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '_' && c != '-' && c != '.') return 0;
    }
    return 1;
}

/* Удаляет пользователя из файла (первое поле == name). Атомарно. */
static int filter_file(const char *path, const char *name, mode_t mode)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) { fclose(f); return -1; }
    fchmod(fd, mode);
    FILE *o = fdopen(fd, "w");
    if (!o) { fclose(f); close(fd); unlink(tmp); return -1; }

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
    if (fflush(o) != 0 || fsync(fileno(o)) != 0) {
        fclose(o); unlink(tmp); return -1;
    }
    fclose(o);
    if (removed) {
        if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    } else {
        unlink(tmp);
    }
    return removed;
}

/* Удаляет пользователя из списков участников /etc/group. */
static int filter_group_members(const char *path, const char *name)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) { fclose(f); return -1; }
    fchmod(fd, 0644);
    FILE *o = fdopen(fd, "w");
    if (!o) { fclose(f); close(fd); unlink(tmp); return -1; }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", line);
        buf[strcspn(buf, "\r\n")] = 0;
        char *save = NULL;
        char *gname = strtok_r(buf, ":", &save);
        char *gpass = strtok_r(NULL, ":", &save);
        char *ggid = strtok_r(NULL, ":", &save);
        char *members = strtok_r(NULL, "", &save);
        if (!gname || !gpass || !ggid || !members) {
            fputs(line, o);
            continue;
        }

        char out_members[1024] = "";
        char *ml = strdup(members);
        if (ml) {
            char *sv = NULL;
            for (char *m = strtok_r(ml, ",", &sv); m;
                 m = strtok_r(NULL, ",", &sv)) {
                if (!strcmp(m, name)) continue;
                if (out_members[0]) strncat(out_members, ",",
                                            sizeof(out_members) - strlen(out_members) - 1);
                strncat(out_members, m,
                        sizeof(out_members) - strlen(out_members) - 1);
            }
            free(ml);
        }
        fprintf(o, "%s:%s:%s:%s\n", gname, gpass, ggid, out_members);
    }
    fclose(f);
    if (fflush(o) != 0 || fsync(fileno(o)) != 0) {
        fclose(o); unlink(tmp); return -1;
    }
    fclose(o);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

static int rm_cb(const char *path, const struct stat *sb, int flag,
                 struct FTW *ftw)
{
    (void)sb; (void)ftw;
    if (flag == FTW_DP || flag == FTW_F || flag == FTW_SL)
        return remove(path);
    return 0;
}

int main(int argc, char **argv)
{
    int remove_home = 0;
    const char *name = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) remove_home = 1;
        else if (!name) name = argv[i];
        else { fprintf(stderr, "userdel: лишний аргумент: %s\n", argv[i]); return 2; }
    }

    if (!name) { fprintf(stderr, "usage: userdel [-r] <user>\n"); return 2; }
    if (!valid_name(name)) {
        fprintf(stderr, "userdel: невалидное имя\n");
        return 1;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "userdel: нужны права root\n");
        return 1;
    }

    struct passwd *pw = getpwnam(name);
    if (!pw) { fprintf(stderr, "userdel: %s не найден\n", name); return 1; }
    if (pw->pw_uid == 0) {
        fprintf(stderr, "userdel: нельзя удалить root\n");
        return 1;
    }

    char home[256];
    snprintf(home, sizeof(home), "%s", pw->pw_dir);

    if (filter_file("/etc/passwd", name, 0644) < 0) {
        perror("/etc/passwd");
        return 1;
    }
    filter_file("/etc/shadow", name, 0600);
    filter_group_members("/etc/group", name);

    if (remove_home && home[0] == '/' && strcmp(home, "/") != 0) {
        /* Никаких system(): рекурсивное удаление своими силами. */
        if (nftw(home, rm_cb, 16, FTW_DEPTH | FTW_PHYS) != 0)
            fprintf(stderr, "userdel: не удалось удалить %s: %s\n",
                    home, strerror(errno));
    }

    printf("userdel: %s удалён\n", name);
    return 0;
}
