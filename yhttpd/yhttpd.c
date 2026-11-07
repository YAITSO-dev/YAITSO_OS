#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 8192
#define MAX_PATH 2048

static volatile sig_atomic_t g_stop = 0;
static const char *g_root = ".";
static int g_port = 8080;
static int g_verbose = 0;

static void on_signal(int s) { (void)s; g_stop = 1; }

static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    dot++;
    if (!strcmp(dot, "html") || !strcmp(dot, "htm")) return "text/html";
    if (!strcmp(dot, "txt"))  return "text/plain";
    if (!strcmp(dot, "css"))  return "text/css";
    if (!strcmp(dot, "js"))   return "application/javascript";
    if (!strcmp(dot, "json")) return "application/json";
    if (!strcmp(dot, "png"))  return "image/png";
    if (!strcmp(dot, "jpg") || !strcmp(dot, "jpeg")) return "image/jpeg";
    if (!strcmp(dot, "gif"))  return "image/gif";
    if (!strcmp(dot, "svg"))  return "image/svg+xml";
    if (!strcmp(dot, "yat"))  return "application/x-yaitso-package";
    if (!strcmp(dot, "sig"))  return "application/octet-stream";
    if (!strcmp(dot, "tar"))  return "application/x-tar";
    if (!strcmp(dot, "gz"))   return "application/gzip";
    return "application/octet-stream";
}

static void url_decode(char *s)
{
    char *p = s;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            int hi = 0, lo = 0;
            char c = s[1];
            if (c >= '0' && c <= '9') hi = c - '0';
            else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
            c = s[2];
            if (c >= '0' && c <= '9') lo = c - '0';
            else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
            *p++ = (char)((hi << 4) | lo);
            s += 3;
        } else if (*s == '+') {
            *p++ = ' ';
            s++;
        } else {
            *p++ = *s++;
        }
    }
    *p = 0;
}

static int path_has_traversal(const char *path)
{
    return strstr(path, "..") != NULL;
}

static void send_error(int c, int code, const char *msg)
{
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "<html><body><h1>%d %s</h1></body></html>\n",
                     code, msg);
    char header[512];
    int h = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", code, msg, n);
    write(c, header, (size_t)h);
    write(c, body, (size_t)n);
}

static void serve_file(int c, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { send_error(c, 404, "Not Found"); return; }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        send_error(c, 404, "Not Found");
        return;
    }

    char header[1024];
    const char *mime = mime_type(path);
    int h = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n", mime, (long)st.st_size);
    if (write(c, header, (size_t)h) < 0) { close(fd); return; }

    char buf[BUF_SIZE];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t sent = 0;
        while (sent < r) {
            ssize_t w = write(c, buf + sent, (size_t)(r - sent));
            if (w <= 0) { close(fd); return; }
            sent += w;
        }
    }
    close(fd);
}

static void handle_request(int c)
{
    char buf[BUF_SIZE];
    ssize_t n = read(c, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = 0;

    if (g_verbose) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = 0;
        fprintf(stderr, "httpd: %s\n", buf);
    }

    if (strncmp(buf, "GET ", 4) != 0 && strncmp(buf, "HEAD ", 5) != 0) {
        send_error(c, 405, "Method Not Allowed");
        return;
    }

    const char *path_start = strchr(buf, ' ') + 1;
    const char *path_end = strchr(path_start, ' ');
    if (!path_end) { send_error(c, 400, "Bad Request"); return; }

    char url[MAX_PATH];
    size_t plen = (size_t)(path_end - path_start);
    if (plen >= sizeof(url)) { send_error(c, 414, "URI Too Long"); return; }
    memcpy(url, path_start, plen);
    url[plen] = 0;

    char *query = strchr(url, '?');
    if (query) *query = 0;

    url_decode(url);

    if (path_has_traversal(url)) { send_error(c, 403, "Forbidden"); return; }

    if (url[0] == '/') url[0] = '_';

    char full[MAX_PATH * 2];
    snprintf(full, sizeof(full), "%s/%s", g_root, url);

    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(full, sizeof(full), "%s/%s/index.html", g_root, url);
    }

    serve_file(c, full);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) g_root = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) g_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            g_verbose = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: yhttpd [--root DIR] [--port N] [-v]\n");
            return 0;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)g_port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv);
        return 1;
    }

    if (listen(srv, 32) < 0) { perror("listen"); close(srv); return 1; }

    fprintf(stderr, "yhttpd: root=%s port=%d\n", g_root, g_port);

    while (!g_stop) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int c = accept(srv, (struct sockaddr *)&cli, &clen);
        if (c < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            handle_request(c);
            close(c);
            _exit(0);
        }
        close(c);
    }

    close(srv);
    return 0;
}
