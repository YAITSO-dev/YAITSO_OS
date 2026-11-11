#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

static struct termios g_saved_termios;
static int            g_raw_active = 0;
static int            g_winch_flag = 0;
static int            g_term_w = 80;

static void on_winch(int sig)
{
    (void)sig;
    g_winch_flag = 1;
}

static int term_w(void)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

static void raw_enable(void)
{
    if (g_raw_active) return;
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) return;
    struct termios raw = g_saved_termios;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= (tcflag_t)CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) g_raw_active = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    g_term_w = term_w();
}

static void raw_disable(void)
{
    if (!g_raw_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
    g_raw_active = 0;
}

struct edit_line {
    char  *buf;
    size_t len;
    size_t cap;
    size_t pos;
    size_t prompt_w;
};

static void el_init(struct edit_line *e, const char *prompt)
{
    e->cap = 128;
    e->buf = xmalloc(e->cap);
    e->buf[0] = 0;
    e->len = 0;
    e->pos = 0;
    e->prompt_w = (size_t)utf8_len(prompt);
}

static void el_free(struct edit_line *e)
{
    xfree(e->buf);
}

static void el_reserve(struct edit_line *e, size_t need)
{
    if (e->len + need + 1 > e->cap) {
        while (e->len + need + 1 > e->cap) e->cap *= 2;
        e->buf = xrealloc(e->buf, e->cap);
    }
}

static void el_insert(struct edit_line *e, const char *s, size_t n)
{
    el_reserve(e, n);
    memmove(e->buf + e->pos + n, e->buf + e->pos, e->len - e->pos + 1);
    memcpy(e->buf + e->pos, s, n);
    e->len += n;
    e->pos += n;
}

static void el_insert_ch(struct edit_line *e, char c)
{
    el_insert(e, &c, 1);
}

static void el_backspace(struct edit_line *e)
{
    if (e->pos == 0) return;
    size_t i = e->pos;
    while (i > 0 && ((unsigned char)e->buf[i-1] & 0xc0) == 0x80) i--;
    if (i > 0) i--;
    size_t n = e->pos - i;
    memmove(e->buf + e->pos - n, e->buf + e->pos, e->len - e->pos + 1);
    e->len -= n;
    e->pos -= n;
}

static void el_delete(struct edit_line *e)
{
    if (e->pos >= e->len) return;
    size_t i = e->pos + 1;
    while (i < e->len && ((unsigned char)e->buf[i] & 0xc0) == 0x80) i++;
    size_t n = i - e->pos;
    memmove(e->buf + e->pos, e->buf + e->pos + n, e->len - e->pos - n + 1);
    e->len -= n;
}

static void el_move_left(struct edit_line *e)
{
    if (e->pos == 0) return;
    do { e->pos--; } while (e->pos > 0 &&
        ((unsigned char)e->buf[e->pos] & 0xc0) == 0x80);
}

static void el_move_right(struct edit_line *e)
{
    if (e->pos >= e->len) return;
    do { e->pos++; } while (e->pos < e->len &&
        ((unsigned char)e->buf[e->pos] & 0xc0) == 0x80);
}

static void el_move_home(struct edit_line *e) { e->pos = 0; }
static void el_move_end(struct edit_line *e)  { e->pos = e->len; }

static void el_kill_to_end(struct edit_line *e)
{
    e->buf[e->pos] = 0;
    e->len = e->pos;
}

static void el_kill_to_start(struct edit_line *e)
{
    if (e->pos == 0) return;
    memmove(e->buf, e->buf + e->pos, e->len - e->pos + 1);
    e->len -= e->pos;
    e->pos = 0;
}

static void el_kill_word_left(struct edit_line *e)
{
    if (e->pos == 0) return;
    size_t i = e->pos;
    while (i > 0 && e->buf[i-1] == ' ') i--;
    while (i > 0 && e->buf[i-1] != ' ') i--;
    size_t n = e->pos - i;
    memmove(e->buf + i, e->buf + e->pos, e->len - e->pos + 1);
    e->len -= n;
    e->pos = i;
}

static void __attribute__((unused)) el_kill_word_right(struct edit_line *e)
{
    if (e->pos >= e->len) return;
    size_t i = e->pos;
    while (i < e->len && e->buf[i] == ' ') i++;
    while (i < e->len && e->buf[i] != ' ') i++;
    size_t n = i - e->pos;
    memmove(e->buf + e->pos, e->buf + i, e->len - i + 1);
    e->len -= n;
}

static void put_str(const char *s) { fputs(s, stdout); }

static void refresh(struct edit_line *e, const char *prompt)
{
    put_str("\r");
    put_str("\033[K");
    put_str(prompt);
    put_str(e->buf);

    size_t tail = e->len - e->pos;
    if (tail > 0) {
        char seq[32];
        snprintf(seq, sizeof(seq), "\033[%zuD", tail);
        put_str(seq);
    }
    fflush(stdout);
}

static void completion_beep(void) { put_str("\a"); fflush(stdout); }

static void complete_path(struct edit_line *e)
{
    size_t i = e->pos;
    while (i > 0 && e->buf[i-1] != ' ' && e->buf[i-1] != '\t') i--;
    size_t wlen = e->pos - i;
    if (wlen == 0) { completion_beep(); return; }

    char *word = xstrndup(e->buf + i, wlen);

    char dir[1024], pat[512];
    const char *slash = strrchr(word, '/');
    if (slash) {
        size_t dl = (size_t)(slash - word);
        if (dl >= sizeof(dir)) { xfree(word); return; }
        memcpy(dir, word, dl);
        dir[dl] = 0;
        if (!dir[0]) { dir[0] = '/'; dir[1] = 0; }
        snprintf(pat, sizeof(pat), "%s", slash + 1);
    } else {
        snprintf(dir, sizeof(dir), ".");
        snprintf(pat, sizeof(pat), "%s", word);
    }

    DIR *d = opendir(dir);
    if (!d) { xfree(word); completion_beep(); return; }

    char **matches = xmalloc(sizeof(char *) * 16);
    int nm = 0, cap = 16;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' && pat[0] != '.') continue;
        if (strncmp(de->d_name, pat, strlen(pat)) != 0) continue;
        if (nm >= cap) { cap *= 2; matches = xrealloc(matches, sizeof(char *) * (size_t)cap); }
        matches[nm++] = xstrdup(de->d_name);
    }
    closedir(d);

    if (nm == 0) { xfree(word); xfree(matches); completion_beep(); return; }

    for (int a = 0; a < nm - 1; a++)
        for (int b = a + 1; b < nm; b++)
            if (strcmp(matches[a], matches[b]) > 0) {
                char *t = matches[a]; matches[a] = matches[b]; matches[b] = t;
            }

    char common[512];
    snprintf(common, sizeof(common), "%s", matches[0]);
    for (int a = 1; a < nm; a++) {
        size_t j = 0;
        while (common[j] && matches[a][j] && common[j] == matches[a][j]) j++;
        common[j] = 0;
    }

    if (nm == 1) {
        size_t cl = strlen(common);
        if (cl > strlen(pat)) {
            size_t pl = strlen(pat);
            if (e->pos - pl == i) {
                el_reserve(e, cl - pl);
                memmove(e->buf + i + cl, e->buf + e->pos, e->len - e->pos + 1);
                memcpy(e->buf + i, common, cl);
                e->len += cl - pl;
                e->pos += cl - pl;
            }
        }
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, common);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            el_insert_ch(e, '/');
        else
            el_insert_ch(e, ' ');
    } else {
        size_t cl = strlen(common);
        if (cl > strlen(pat)) {
            size_t pl = strlen(pat);
            if (e->pos - pl == i) {
                el_reserve(e, cl - pl);
                memmove(e->buf + i + cl, e->buf + e->pos, e->len - e->pos + 1);
                memcpy(e->buf + i, common, cl);
                e->len += cl - pl;
                e->pos += cl - pl;
            }
        } else {
            put_str("\r\n");
            for (int a = 0; a < nm; a++) {
                put_str(matches[a]);
                put_str("  ");
            }
            put_str("\r\n");
        }
    }

    for (int a = 0; a < nm; a++) xfree(matches[a]);
    xfree(matches);
    xfree(word);
}

static void complete_cmd(struct edit_line *e)
{
    static const char *cmds[] = {
        "help", "version", "about", "welcome", "clear", "reset",
        "echo", "printf", "true", "false", "date", "uname", "whoami",
        "history", "sleep", "time", "set", "export", "unset", "env",
        "alias", "unalias", "which", "type", "jobs", "fg", "bg",
        "wait", "shift", "cd", "pwd", "ls", "cat", "head", "tail",
        "wc", "grep", "seq", "pushd", "popd", "dirs", "source",
        "read", "exit", "logout", "yat", "svc", "ywifi", NULL
    };
    size_t i = 0;
    while (i < e->pos && e->buf[i] != ' ') i++;
    size_t wlen = e->pos;
    if (wlen == 0) { completion_beep(); return; }

    char *word = xstrndup(e->buf, wlen);
    char common[64] = "";
    int nm = 0;
    for (int a = 0; cmds[a]; a++) {
        if (strncmp(cmds[a], word, wlen) != 0) continue;
        nm++;
        if (nm == 1) snprintf(common, sizeof(common), "%s", cmds[a]);
        else {
            size_t j = 0;
            while (common[j] && cmds[a][j] && common[j] == cmds[a][j]) j++;
            common[j] = 0;
        }
    }
    xfree(word);

    if (nm == 0) { completion_beep(); return; }
    if (nm == 1) {
        size_t cl = strlen(common);
        if (cl >= e->len) {
            el_reserve(e, cl - e->len + 2);
            memcpy(e->buf, common, cl);
            e->len = cl;
            e->buf[e->len] = 0;
            e->pos = cl;
        }
        el_insert_ch(e, ' ');
    } else {
        size_t cl = strlen(common);
        if (cl > 0) {
            memcpy(e->buf, common, cl);
            e->len = cl;
            e->buf[e->len] = 0;
            e->pos = cl;
        }
    }
}

static void hist_prev(struct edit_line *e, struct yaitso_ctx *ctx, int *hpos)
{
    int total = history_count();
    if (total == 0) return;
    if (*hpos == -1) *hpos = total;
    if (*hpos <= 0) return;
    (*hpos)--;
    const char *line = history_get(ctx, *hpos);
    if (!line) return;
    size_t n = strlen(line);
    if (n + 1 > e->cap) {
        while (n + 1 > e->cap) e->cap *= 2;
        e->buf = xrealloc(e->buf, e->cap);
    }
    memcpy(e->buf, line, n + 1);
    e->len = n;
    e->pos = n;
}

static void hist_next(struct edit_line *e, struct yaitso_ctx *ctx, int *hpos)
{
    int total = history_count();
    if (*hpos < 0) return;
    if (*hpos + 1 >= total) {
        *hpos = -1;
        e->buf[0] = 0;
        e->len = 0;
        e->pos = 0;
        return;
    }
    (*hpos)++;
    const char *line = history_get(ctx, *hpos);
    if (!line) return;
    size_t n = strlen(line);
    if (n + 1 > e->cap) {
        while (n + 1 > e->cap) e->cap *= 2;
        e->buf = xrealloc(e->buf, e->cap);
    }
    memcpy(e->buf, line, n + 1);
    e->len = n;
    e->pos = n;
}

static int reverse_search(struct edit_line *e, const char *prompt,
                          struct yaitso_ctx *ctx)
{
    char query[128] = "";
    size_t qlen = 0;
    int found_idx = -1;

    for (;;) {
        put_str("\r\033[K");
        put_str("(reverse-i-search)`");
        put_str(query);
        put_str("': ");
        if (found_idx >= 0) {
            const char *l = history_get(ctx, found_idx);
            if (l) put_str(l);
        }
        fflush(stdout);

        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1) return 0;

        if (c == 0x1b) {
            if (found_idx >= 0) {
                const char *l = history_get(ctx, found_idx);
                if (l) {
                    size_t n = strlen(l);
                    if (n + 1 > e->cap) {
                        while (n + 1 > e->cap) e->cap *= 2;
                        e->buf = xrealloc(e->buf, e->cap);
                    }
                    memcpy(e->buf, l, n + 1);
                    e->len = n;
                    e->pos = n;
                }
            }
            put_str("\r\033[K");
            refresh(e, prompt);
            return 1;
        }
        if (c == 0x03 || c == 0x07) {
            put_str("\r\033[K");
            refresh(e, prompt);
            return 0;
        }
        if (c == '\n' || c == '\r') { put_str("\r\033[K"); return 1; }
        if (c == 0x12) {
            int start = (found_idx < 0) ? history_count() - 1 : found_idx - 1;
            for (int i = start; i >= 0; i--) {
                const char *l = history_get(ctx, i);
                if (l && strstr(l, query)) { found_idx = i; break; }
            }
            continue;
        }
        if (c == 0x7f || c == 0x08) {
            if (qlen > 0) { qlen--; query[qlen] = 0; }
            found_idx = -1;
            if (qlen > 0) {
                for (int i = history_count() - 1; i >= 0; i--) {
                    const char *l = history_get(ctx, i);
                    if (l && strstr(l, query)) { found_idx = i; break; }
                }
            }
            continue;
        }
        if (c >= 32 && c < 127) {
            if (qlen < sizeof(query) - 1) {
                query[qlen++] = (char)c;
                query[qlen] = 0;
            }
            for (int i = history_count() - 1; i >= 0; i--) {
                const char *l = history_get(ctx, i);
                if (l && strstr(l, query)) { found_idx = i; break; }
            }
            continue;
        }
    }
}

char *readline_yaitso(struct yaitso_ctx *ctx, const char *prompt)
{
    if (!ctx->interactive) {
        char line[8192];
        if (!fgets(line, sizeof(line), stdin)) return NULL;
        line[strcspn(line, "\r\n")] = 0;
        return xstrdup(line);
    }

    raw_enable();

    struct edit_line e;
    el_init(&e, prompt);

    int hpos = -1;
    refresh(&e, prompt);

    for (;;) {
        if (g_winch_flag) {
            g_winch_flag = 0;
            g_term_w = term_w();
            refresh(&e, prompt);
        }

        unsigned char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            el_free(&e);
            raw_disable();
            return NULL;
        }

        if (c == '\r' || c == '\n') {
            put_str("\r\n");
            fflush(stdout);
            el_reserve(&e, 0);
            char *out = xstrdup(e.buf);
            el_free(&e);
            raw_disable();
            return out;
        }
        if (c == 0x04) {
            if (e.len == 0) {
                put_str("\r\n");
                el_free(&e);
                raw_disable();
                return NULL;
            }
            el_delete(&e);
            refresh(&e, prompt);
            continue;
        }
        if (c == 0x03) {
            put_str("\r\n");
            fflush(stdout);
            e.len = 0; e.pos = 0; e.buf[0] = 0;
            hpos = -1;
            refresh(&e, prompt);
            continue;
        }
        if (c == 0x0c) {
            put_str("\033[2J\033[H");
            refresh(&e, prompt);
            continue;
        }
        if (c == 0x0b) { el_kill_to_end(&e);   refresh(&e, prompt); continue; }
        if (c == 0x15) { el_kill_to_start(&e); refresh(&e, prompt); continue; }
        if (c == 0x17) { el_kill_word_left(&e); refresh(&e, prompt); continue; }
        if (c == 0x01) { el_move_home(&e); refresh(&e, prompt); continue; }
        if (c == 0x05) { el_move_end(&e);  refresh(&e, prompt); continue; }
        if (c == 0x02) { el_move_left(&e); refresh(&e, prompt); continue; }
        if (c == 0x06) { el_move_right(&e); refresh(&e, prompt); continue; }
        if (c == 0x09) {
            if (e.pos == 0) complete_cmd(&e);
            else complete_path(&e);
            refresh(&e, prompt);
            continue;
        }
        if (c == 0x12) { reverse_search(&e, prompt, ctx); continue; }
        if (c == 0x10) { hist_prev(&e, ctx, &hpos); refresh(&e, prompt); continue; }
        if (c == 0x0e) { hist_next(&e, ctx, &hpos); refresh(&e, prompt); continue; }
        if (c == 0x7f || c == 0x08) {
            el_backspace(&e);
            hpos = -1;
            refresh(&e, prompt);
            continue;
        }
        if (c == 0x1b) {
            unsigned char seq[8] = {0};
            if (read(STDIN_FILENO, seq, 1) != 1) continue;
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, seq + 1, 1) != 1) continue;
                if (seq[1] == 'A') { hist_prev(&e, ctx, &hpos); refresh(&e, prompt); continue; }
                if (seq[1] == 'B') { hist_next(&e, ctx, &hpos); refresh(&e, prompt); continue; }
                if (seq[1] == 'C') { el_move_right(&e); refresh(&e, prompt); continue; }
                if (seq[1] == 'D') { el_move_left(&e);  refresh(&e, prompt); continue; }
                if (seq[1] == 'H') { el_move_home(&e);  refresh(&e, prompt); continue; }
                if (seq[1] == 'F') { el_move_end(&e);   refresh(&e, prompt); continue; }
                if (seq[1] == '1') {
                    unsigned char rest[4] = {0};
                    if (read(STDIN_FILENO, rest, 2) != 2) continue;
                    if (rest[0] == '~') { el_move_home(&e); refresh(&e, prompt); }
                    else if (rest[0] == ';' && rest[1] == '5') {
                        unsigned char last;
                        if (read(STDIN_FILENO, &last, 1) == 1 && last == 'C') {
                            el_move_right(&e); refresh(&e, prompt);
                        } else if (last == 'D') {
                            el_move_left(&e); refresh(&e, prompt);
                        }
                    }
                    continue;
                }
                if (seq[1] == '3') {
                    unsigned char rest;
                    if (read(STDIN_FILENO, &rest, 1) == 1 && rest == '~') {
                        el_delete(&e); refresh(&e, prompt);
                    }
                    continue;
                }
                if (seq[1] == '4') {
                    unsigned char rest;
                    if (read(STDIN_FILENO, &rest, 1) == 1 && rest == '~') {
                        el_move_end(&e); refresh(&e, prompt);
                    }
                    continue;
                }
                if (seq[1] == '5') {
                    unsigned char rest;
                    if (read(STDIN_FILENO, &rest, 1) == 1 && rest == '~') { }
                    continue;
                }
                if (seq[1] == '6') {
                    unsigned char rest;
                    if (read(STDIN_FILENO, &rest, 1) == 1 && rest == '~') {
                        el_kill_to_end(&e); refresh(&e, prompt);
                    }
                    continue;
                }
            } else if (seq[0] == 'O') {
                if (read(STDIN_FILENO, seq + 1, 1) != 1) continue;
                if (seq[1] == 'H') { el_move_home(&e); refresh(&e, prompt); continue; }
                if (seq[1] == 'F') { el_move_end(&e);  refresh(&e, prompt); continue; }
            }
            continue;
        }

        if (c >= 32) {
            el_insert_ch(&e, (char)c);
            hpos = -1;
            refresh(&e, prompt);
        }
    }
}
