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
#include <sys/file.h>
#include <errno.h>

static int valid_name(const char *s)
{
    if (!s || !*s || strlen(s) >= 32) return 0;
    if (!islower((unsigned char)s[0]) && s[0] != '_') return 0;
    for (const char *p = s; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) &&
            *p != '_' && *p != '-') return 0;
    return 1;
}

static int valid_group_list(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        if (n == 0 || n >= 32) return 0;
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            if (!islower(c) && !isdigit(c) && c != '_' && c != '-') return 0;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return 1;
}

/* Атомарно дописывает строку в файл (mkdir/rename в том же каталоге). */
static int append_line(const char *path, const char *line)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", path);
    int out = mkstemp(tmp);
    if (out < 0) return -1;
    fchmod(out, 0644);

    int in = open(path, O_RDONLY | O_CLOEXEC);
    if (in >= 0) {
        flock(in, LOCK_SH);
        char buf[4096];
        ssize_t r;
        while ((r = read(in, buf, sizeof(buf))) > 0) {
            if (write(out, buf, (size_t)r) != r) {
                close(in); close(out); unlink(tmp); return -1;
            }
        }
        close(in);
    }

    size_t len = strlen(line);
    if (write(out, line, len) != (ssize_t)len ||
        write(out, "\n", 1) != 1) {
        close(out); unlink(tmp); return -1;
    }
    fsync(out);
    close(out);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
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
        if (uid > max && uid < 65534) max = uid;
    }
    fclose(f);
    return max + 1;
}

static int group_exists(const char *name)
{
    FILE *f = fopen("/etc/group", "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        if (!strcmp(line, name)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

/* Добавляет пользователя в дополнительные группы, переписывая /etc/group. */
static int add_to_groups(const char *groups, const char *user)
{
    char tmp[] = "/etc/group.tmpXXXXXX";
    int out = mkstemp(tmp);
    if (out < 0) return -1;
    fchmod(out, 0644);

    FILE *in = fopen("/etc/group", "r");
    if (!in) { close(out); unlink(tmp); return -1; }
    FILE *o = fdopen(out, "w");
    if (!o) { fclose(in); close(out); unlink(tmp); return -1; }

    char *copy = strdup(groups);
    if (!copy) { fclose(in); fclose(o); unlink(tmp); return -1; }

    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", line);
        buf[strcspn(buf, "\r\n")] = 0;
        char *save = NULL;
        char *gname = strtok_r(buf, ":", &save);
        char *gpass = strtok_r(NULL, ":", &save);
        char *ggid = strtok_r(NULL, ":", &save);
        char *members = strtok_r(NULL, "", &save);
        if (!gname || !gpass || !ggid) { fputs(line, o); continue; }

        int wanted = 0;
        char *cl = strdup(copy);
        if (cl) {
            char *sv = NULL;
            for (char *g = strtok_r(cl, ",", &sv); g; g = strtok_r(NULL, ",", &sv))
                if (!strcmp(g, gname)) { wanted = 1; break; }
            free(cl);
        }
        if (wanted) {
            int present = 0;
            if (members) {
                char *ml = strdup(members);
                if (ml) {
                    char *sv = NULL;
                    for (char *m = strtok_r(ml, ",", &sv); m;
                         m = strtok_r(NULL, ",", &sv))
                        if (!strcmp(m, user)) { present = 1; break; }
                    free(ml);
                }
            }
            if (!present) {
                if (members && *members)
                    fprintf(o, "%s:%s:%s:%s,%s\n", gname, gpass, ggid,
                            members, user);
                else
                    fprintf(o, "%s:%s:%s:%s\n", gname, gpass, ggid, user);
            } else {
                fprintf(o, "%s:%s:%s:%s\n", gname, gpass, ggid,
                        members ? members : "");
            }
        } else {
            fputs(line, o);
        }
    }
    free(copy);
    fclose(in);
    if (fflush(o) != 0 || fsync(fileno(o)) != 0) {
        fclose(o); unlink(tmp); return -1;
    }
    fclose(o);
    if (rename(tmp, "/etc/group") != 0) { unlink(tmp); return -1; }
    return 0;
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
        else { fprintf(stderr, "useradd: лишний аргумент: %s\n", argv[i]); return 2; }
    }

    if (!valid_name(name)) {
        fprintf(stderr, "useradd: невалидное имя\n");
        return 1;
    }
    if (group && !valid_group_list(group)) {
        fprintf(stderr, "useradd: невалидный список групп\n");
        return 1;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "useradd: нужны права root\n");
        return 1;
    }

    struct passwd *exists = getpwnam(name);
    if (exists) { fprintf(stderr, "useradd: %s уже существует\n", name); return 1; }

    if (group) {
        char *cl = strdup(group);
        if (!cl) return 1;
        char *sv = NULL;
        for (char *g = strtok_r(cl, ",", &sv); g; g = strtok_r(NULL, ",", &sv)) {
            if (!group_exists(g)) {
                fprintf(stderr, "useradd: группа %s не найдена\n", g);
                free(cl);
                return 1;
            }
        }
        free(cl);
    }

    int uid = read_last_uid();
    int gid = uid;
    if (uid >= 65534) {
        fprintf(stderr, "useradd: нет свободных uid\n");
        return 1;
    }

    char pw_line[512];
    snprintf(pw_line, sizeof(pw_line), "%s:x:%d:%d::/home/%s:/bin/sh",
             name, uid, gid, name);
    if (append_line("/etc/passwd", pw_line) != 0) {
        perror("/etc/passwd");
        return 1;
    }

    char grp_line[256];
    snprintf(grp_line, sizeof(grp_line), "%s:x:%d:", name, gid);
    if (append_line("/etc/group", grp_line) != 0) {
        perror("/etc/group");
        return 1;
    }

    char sh_line[256];
    snprintf(sh_line, sizeof(sh_line), "%s:!:::::::", name);
    if (append_line("/etc/shadow", sh_line) != 0) {
        perror("/etc/shadow");
        return 1;
    }

    if (group && add_to_groups(group, name) != 0) {
        fprintf(stderr, "useradd: не удалось добавить в группы\n");
        return 1;
    }

    if (create_m && create_home(name, uid, gid) != 0) {
        perror("useradd: home");
        return 1;
    }

    printf("useradd: %s создан (uid %d, gid %d)\n", name, uid, gid);
    return 0;
}
