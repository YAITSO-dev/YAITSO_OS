#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>

static int has_glob(const char *s)
{
    while (*s) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
        s++;
    }
    return 0;
}

int glob_expand(const char *pattern, char ***out, int *outn)
{
    if (!pattern || !out || !outn) return -1;
    *out = NULL;
    *outn = 0;
    if (!has_glob(pattern)) return 0;

    const char *slash = strrchr(pattern, '/');
    char dir[1024], pat[512];
    if (slash) {
        size_t dl = (size_t)(slash - pattern);
        if (dl >= sizeof(dir)) return -1;
        memcpy(dir, pattern, dl);
        dir[dl] = 0;
        if (!dir[0]) { dir[0] = '/'; dir[1] = 0; }
        snprintf(pat, sizeof(pat), "%s", slash + 1);
    } else {
        snprintf(dir, sizeof(dir), ".");
        snprintf(pat, sizeof(pat), "%s", pattern);
    }

    DIR *d = opendir(dir);
    if (!d) return 0;

    int cap = 16, n = 0;
    char **arr = xmalloc(sizeof(char *) * (size_t)cap);
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' && pat[0] != '.') continue;
        if (fnmatch(pat, de->d_name, 0) != 0) continue;
        if (n >= cap) {
            cap *= 2;
            arr = xrealloc(arr, sizeof(char *) * (size_t)cap);
        }
        if (slash) {
            size_t dl = strlen(dir), nl = strlen(de->d_name);
            char *s = xmalloc(dl + 1 + nl + 1);
            memcpy(s, dir, dl);
            s[dl] = '/';
            memcpy(s + dl + 1, de->d_name, nl);
            s[dl + 1 + nl] = 0;
            arr[n++] = s;
        } else {
            arr[n++] = xstrdup(de->d_name);
        }
    }
    closedir(d);

    if (n == 0) { xfree(arr); return 0; }

    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(arr[i], arr[j]) > 0) {
                char *t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }

    *out = arr;
    *outn = n;
    return 0;
}
