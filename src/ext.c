#define _GNU_SOURCE
#include "yaitso.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

int ext_command_exists(const char *name)
{
    if (!name || !*name) return 0;
    if (strchr(name, '/'))
        return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    char *copy = xstrdup(path);
    char *save = NULL;
    char *tok = strtok_r(copy, ":", &save);
    int found = 0;
    while (tok) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", tok, name);
        struct stat st;
        if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR)) { found = 1; break; }
        tok = strtok_r(NULL, ":", &save);
    }
    xfree(copy);
    return found;
}
