#define _GNU_SOURCE
#include "ed25519.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static unsigned char *read_all(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_len = r;
    return buf;
}

static int hex_decode(const char *s, unsigned char *out, size_t max)
{
    size_t n = strlen(s);
    if (n % 2) return -1;
    size_t bytes = n / 2;
    if (bytes > max) return -1;
    for (size_t i = 0; i < bytes; i++) {
        int hi = 0, lo = 0;
        char c;
        c = s[i*2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return -1;
        c = s[i*2+1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return (int)bytes;
}

int main(int argc, char **argv)
{
    const char *key_path = "key.sec";
    const char *in_path  = NULL;
    const char *out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) key_path = argv[++i];
        else if (!in_path) in_path = argv[i];
        else if (!out_path) out_path = argv[i];
    }

    if (!in_path) {
        fprintf(stderr, "usage: yat-sign [-k key.sec] <file> [out.sig]\n");
        return 2;
    }
    if (!out_path) {
        static char buf[1024];
        snprintf(buf, sizeof(buf), "%s.sig", in_path);
        out_path = buf;
    }

    FILE *kf = fopen(key_path, "r");
    if (!kf) { perror(key_path); return 1; }
    char hex[256];
    if (!fgets(hex, sizeof(hex), kf)) { fclose(kf); return 1; }
    fclose(kf);
    hex[strcspn(hex, "\r\n")] = 0;

    unsigned char seed[32];
    if (hex_decode(hex, seed, 32) != 32) {
        fprintf(stderr, "yat-sign: невалидный приватный ключ\n");
        return 1;
    }

    unsigned char pk[32];
    ed25519_public_key(seed, pk);

    size_t mlen = 0;
    unsigned char *msg = read_all(in_path, &mlen);
    if (!msg) { perror(in_path); return 1; }

    unsigned char sig[64];
    ed25519_sign(msg, mlen, seed, pk, sig);

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror(out_path); free(msg); return 1; }
    if (write(fd, sig, 64) != 64) { perror("write"); close(fd); free(msg); return 1; }
    close(fd);
    free(msg);

    printf("подписано: %s -> %s\n", in_path, out_path);
    return 0;
}
