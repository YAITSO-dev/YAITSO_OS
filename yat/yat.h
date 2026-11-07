#ifndef YAT_H
#define YAT_H

#include <stddef.h>

struct http_url {
    char host[256];
    char path[1024];
    int  port;
    int  https;
};

struct http_buf {
    char  *data;
    size_t len;
};

struct pkg_meta {
    char name[128];
    char version[64];
    char arch[32];
    long size;
    char depends[1024];
    char desc[512];
    char libc[32];
};

int  http_url_parse(const char *url, struct http_url *u);
int  http_get(const char *url, struct http_buf *out);
int  http_head(const char *url);

int  yat_meta_read(const char *dir, struct pkg_meta *m);
int  yat_meta_write(const char *dir, const struct pkg_meta *m);

#endif
