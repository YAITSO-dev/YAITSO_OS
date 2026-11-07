#define _GNU_SOURCE
#include "yat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

static SSL_CTX *g_tls_ctx = NULL;
static int      g_tls_inited = 0;

static int tls_ctx_init(void)
{
    if (g_tls_inited) return g_tls_ctx ? 0 : -1;
    g_tls_inited = 1;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    g_tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_tls_ctx) return -1;
    SSL_CTX_set_default_verify_paths(g_tls_ctx);
    SSL_CTX_set_verify(g_tls_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_min_proto_version(g_tls_ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(g_tls_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                                  SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    return 0;
}

struct http_conn {
    int          sock;
    SSL         *ssl;
    const char  *host;
};

static SSL *tls_connect(int sock, const char *host)
{
    if (tls_ctx_init() != 0) return NULL;
    SSL *ssl = SSL_new(g_tls_ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host);
    X509_VERIFY_PARAM *p = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(p, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    X509_VERIFY_PARAM_set1_host(p, host, 0);
    if (SSL_connect(ssl) != 1) { SSL_free(ssl); return NULL; }
    return ssl;
}

static ssize_t tls_read(SSL *ssl, void *buf, size_t n)
{
    int r = SSL_read(ssl, buf, (int)n);
    if (r > 0) return r;
    int err = SSL_get_error(ssl, r);
    if (err == SSL_ERROR_ZERO_RETURN) return 0;
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return -2;
    return -1;
}

static ssize_t tls_write(SSL *ssl, const void *buf, size_t n)
{
    int r = SSL_write(ssl, buf, (int)n);
    if (r > 0) return r;
    int err = SSL_get_error(ssl, r);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return -2;
    return -1;
}

static void tls_close(SSL *ssl)
{
    if (!ssl) return;
    SSL_shutdown(ssl);
    SSL_free(ssl);
}

int http_url_parse(const char *url, struct http_url *u)
{
    memset(u, 0, sizeof(*u));
    if (!strncmp(url, "https://", 8)) { u->https = 1; url += 8; }
    else if (!strncmp(url, "http://", 7)) { u->https = 0; url += 7; }
    else u->https = 0;

    const char *slash = strchr(url, '/');
    size_t hlen = slash ? (size_t)(slash - url) : strlen(url);
    if (hlen >= sizeof(u->host)) return -1;
    memcpy(u->host, url, hlen);
    u->host[hlen] = 0;

    char *colon = strchr(u->host, ':');
    if (colon) { *colon = 0; u->port = atoi(colon + 1); }
    if (!u->port) u->port = u->https ? 443 : 80;

    if (slash) snprintf(u->path, sizeof(u->path), "%s", slash);
    else       snprintf(u->path, sizeof(u->path), "/");
    return 0;
}

static int tcp_connect(const char *host, int port)
{
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        struct timeval tv = {15, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int build_request(char *buf, size_t n, const char *method,
                         const struct http_url *u)
{
    int r = snprintf(buf, n,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: yat/0.3.1\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, u->path, u->host);
    return (r > 0 && (size_t)r < n) ? 0 : -1;
}

static ssize_t my_read(struct http_conn *c, void *buf, size_t n)
{
    if (c->ssl) return tls_read(c->ssl, buf, n);
    ssize_t r = read(c->sock, buf, n);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
    return r;
}

static ssize_t my_write(struct http_conn *c, const void *buf, size_t n)
{
    if (c->ssl) return tls_write(c->ssl, buf, n);
    ssize_t r = write(c->sock, buf, n);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
    return r;
}

static int read_headers(struct http_conn *c, char *buf, size_t cap, size_t *got)
{
    *got = 0;
    for (;;) {
        if (*got >= cap - 1) return -1;
        ssize_t r = my_read(c, buf + *got, cap - 1 - *got);
        if (r == 0) { buf[*got] = 0; return 0; }
        if (r == -2) continue;
        if (r < 0) return -1;
        *got += (size_t)r;
        buf[*got] = 0;
        if (strstr(buf, "\r\n\r\n")) return 0;
    }
}

static int response_code(const char *headers)
{
    const char *sp = strchr(headers, ' ');
    return sp ? atoi(sp + 1) : -1;
}

int http_get(const char *url, struct http_buf *out)
{
    out->data = NULL;
    out->len = 0;
    struct http_url u;
    if (http_url_parse(url, &u) != 0) return -1;

    int sock = tcp_connect(u.host, u.port);
    if (sock < 0) return -1;

    struct http_conn c = { sock, NULL, u.host };
    if (u.https) {
        c.ssl = tls_connect(sock, u.host);
        if (!c.ssl) { close(sock); return -1; }
    }

    char req[1024];
    if (build_request(req, sizeof(req), "GET", &u) != 0) {
        tls_close(c.ssl); close(sock); return -1;
    }
    size_t sent = 0, rl = strlen(req);
    while (sent < rl) {
        ssize_t w = my_write(&c, req + sent, rl - sent);
        if (w == -2) continue;
        if (w <= 0) { tls_close(c.ssl); close(sock); return -1; }
        sent += (size_t)w;
    }

    size_t hcap = 131072, hgot = 0;
    char *headers = malloc(hcap);
    if (!headers) { tls_close(c.ssl); close(sock); return -1; }
    if (read_headers(&c, headers, hcap, &hgot) != 0) {
        free(headers); tls_close(c.ssl); close(sock); return -1;
    }
    int code = response_code(headers);
    char *body_start = strstr(headers, "\r\n\r\n");
    if (body_start) body_start += 4;
    size_t body_prefix = body_start ? hgot - (size_t)(body_start - headers) : 0;

    if (code >= 300 && code < 400) {
        free(headers); tls_close(c.ssl); close(sock);
        return -2;
    }
    if (code != 200) {
        free(headers); tls_close(c.ssl); close(sock); return -1;
    }

    size_t bcap = 1024 * 1024;
    char *body = malloc(bcap);
    if (!body) { free(headers); tls_close(c.ssl); close(sock); return -1; }
    size_t blen = 0;
    if (body_prefix) {
        if (body_prefix > bcap) {
            free(headers); free(body); tls_close(c.ssl); close(sock); return -1;
        }
        memcpy(body, body_start, body_prefix);
        blen = body_prefix;
    }
    for (;;) {
        if (blen + 4096 > bcap) {
            bcap *= 2;
            char *nb = realloc(body, bcap);
            if (!nb) {
                free(headers); free(body); tls_close(c.ssl); close(sock);
                return -1;
            }
            body = nb;
        }
        ssize_t r = my_read(&c, body + blen, bcap - blen - 1);
        if (r == 0) break;
        if (r == -2) continue;
        if (r < 0) {
            free(headers); free(body); tls_close(c.ssl); close(sock);
            return -1;
        }
        blen += (size_t)r;
    }
    body[blen] = 0;

    free(headers);
    tls_close(c.ssl);
    close(sock);

    out->data = body;
    out->len = blen;
    return 0;
}

int http_head(const char *url)
{
    struct http_url u;
    if (http_url_parse(url, &u) != 0) return -1;
    int sock = tcp_connect(u.host, u.port);
    if (sock < 0) return -1;
    struct http_conn c = { sock, NULL, u.host };
    if (u.https) {
        c.ssl = tls_connect(sock, u.host);
        if (!c.ssl) { close(sock); return -1; }
    }
    char req[1024];
    if (build_request(req, sizeof(req), "HEAD", &u) != 0) {
        tls_close(c.ssl); close(sock); return -1;
    }
    size_t sent = 0, rl = strlen(req);
    while (sent < rl) {
        ssize_t w = my_write(&c, req + sent, rl - sent);
        if (w == -2) continue;
        if (w <= 0) { tls_close(c.ssl); close(sock); return -1; }
        sent += (size_t)w;
    }
    char buf[8192];
    size_t got = 0;
    if (read_headers(&c, buf, sizeof(buf), &got) != 0) {
        tls_close(c.ssl); close(sock); return -1;
    }
    int code = response_code(buf);
    tls_close(c.ssl); close(sock);
    return code;
}
