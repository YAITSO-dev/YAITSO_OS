#define _GNU_SOURCE
#include "yat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' ||
                     e[-1] == ' '  || e[-1] == '\t')) *--e = 0;
    return s;
}

int yat_meta_read(const char *dir, struct pkg_meta *m)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/META", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(m, 0, sizeof(*m));
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p) continue;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        *colon = 0;
        char *k = trim(p);
        char *v = trim(colon + 1);

        if      (!strcmp(k, "name"))    snprintf(m->name, sizeof(m->name), "%s", v);
        else if (!strcmp(k, "version")) snprintf(m->version, sizeof(m->version), "%s", v);
        else if (!strcmp(k, "arch"))    snprintf(m->arch, sizeof(m->arch), "%s", v);
        else if (!strcmp(k, "size"))    m->size = atol(v);
        else if (!strcmp(k, "depends")) snprintf(m->depends, sizeof(m->depends), "%s", v);
        else if (!strcmp(k, "desc"))    snprintf(m->desc, sizeof(m->desc), "%s", v);
        else if (!strcmp(k, "libc"))    snprintf(m->libc, sizeof(m->libc), "%s", v);
    }
    fclose(f);
    return m->name[0] ? 0 : -1;
}

int yat_meta_write(const char *dir, const struct pkg_meta *m)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/META", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "name: %s\n", m->name);
    fprintf(f, "version: %s\n", m->version);
    fprintf(f, "arch: %s\n", m->arch);
    fprintf(f, "size: %ld\n", m->size);
    fprintf(f, "depends: %s\n", m->depends);
    fprintf(f, "desc: %s\n", m->desc);
    if (m->libc[0]) fprintf(f, "libc: %s\n", m->libc);
    fclose(f);
    return 0;
}
