#define _GNU_SOURCE
#include "ed25519.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static int random_bytes(unsigned char *buf, size_t n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) { close(fd); return -1; }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

static void hex_encode(const unsigned char *in, size_t n, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i*2]   = hex[in[i] >> 4];
        out[i*2+1] = hex[in[i] & 0xf];
    }
    out[n*2] = 0;
}

int main(int argc, char **argv)
{
    const char *priv_path = "key.sec";
    const char *pub_path  = "key.pub";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) priv_path = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) pub_path = argv[++i];
    }

    unsigned char seed[32];
    if (random_bytes(seed, 32) != 0) {
        fprintf(stderr, "yat-keygen: /dev/urandom недоступен\n");
        return 1;
    }

    unsigned char pk[32];
    ed25519_public_key(seed, pk);

    char seed_hex[65];
    char pk_hex[65];
    hex_encode(seed, 32, seed_hex);
    hex_encode(pk, 32, pk_hex);

    int fd = open(priv_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror(priv_path); return 1; }
    char line[200];
    int n = snprintf(line, sizeof(line), "%s\n", seed_hex);
    if (write(fd, line, (size_t)n) < 0) { perror("write"); close(fd); return 1; }
    close(fd);

    fd = open(pub_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror(pub_path); return 1; }
    n = snprintf(line, sizeof(line), "%s\n", pk_hex);
    if (write(fd, line, (size_t)n) < 0) { perror("write"); close(fd); return 1; }
    close(fd);

    printf("private key: %s\n", priv_path);
    printf("public key:  %s\n", pub_path);
    printf("public hex:  %s\n", pk_hex);
    return 0;
}
