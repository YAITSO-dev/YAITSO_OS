#define _GNU_SOURCE
#include "yterm.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

static struct yt_session g_sess;
static ywt_surface_t  *g_surf = NULL;
static ywt_font_t     *g_font = NULL;
static ywt_buf_t      *g_buf = NULL;
static int             g_need_redraw = 1;
static int             g_closing = 0;
static int             g_cols = 90;
static int             g_rows = 25;
static struct yt_search g_search;
static int             g_show_tabs = 1;

static struct yt_tab *active_tab(void)
{
    if (g_sess.active < 0 || g_sess.active >= g_sess.ntabs) return NULL;
    return &g_sess.tabs[g_sess.active];
}

static void key_to_input(xkb_keysym_t sym, uint32_t mods,
                         const char *utf8)
{
    char buf[16];
    int n = 0;
    struct yt_tab *t = active_tab();
    if (!t) return;

    if (sym == XKB_KEY_Return)     { buf[0] = '\r'; n = 1; }
    else if (sym == XKB_KEY_BackSpace) { buf[0] = 0x7f; n = 1; }
    else if (sym == XKB_KEY_Tab)   { buf[0] = '\t'; n = 1; }
    else if (sym == XKB_KEY_Escape){ buf[0] = 0x1b; n = 1; }
    else if (sym == XKB_KEY_Up)    { memcpy(buf, "\033[A", 3); n = 3; }
    else if (sym == XKB_KEY_Down)  { memcpy(buf, "\033[B", 3); n = 3; }
    else if (sym == XKB_KEY_Right) { memcpy(buf, "\033[C", 3); n = 3; }
    else if (sym == XKB_KEY_Left)  { memcpy(buf, "\033[D", 3); n = 3; }
    else if (sym == XKB_KEY_Home)  { memcpy(buf, "\033[H", 3); n = 3; }
    else if (sym == XKB_KEY_End)   { memcpy(buf, "\033[F", 3); n = 3; }
    else if (sym == XKB_KEY_Delete){ memcpy(buf, "\033[3~", 4); n = 4; }
    else if (utf8 && utf8[0]) {
        size_t l = strlen(utf8);
        if (l >= sizeof(buf)) l = sizeof(buf) - 1;
        memcpy(buf, utf8, l);
        n = (int)l;
    } else {
        return;
    }

    if (t->master_fd >= 0 && n > 0) write(t->master_fd, buf, (size_t)n);
    (void)mods;
}

static void on_key(void *data, xkb_keysym_t sym, uint32_t keycode,
                   uint32_t state, uint32_t mods, const char *utf8)
{
    (void)data; (void)keycode;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    if (sym == XKB_KEY_d && (mods & YWT_MOD_CTRL) &&
        (mods & YWT_MOD_SHIFT)) { g_closing = 1; return; }
    if (sym == XKB_KEY_Escape && (mods & YWT_MOD_CTRL)) { g_closing = 1; return; }

    if (sym == XKB_KEY_t && (mods & YWT_MOD_CTRL) &&
        (mods & YWT_MOD_SHIFT)) {
        char *shell = getenv("SHELL");
        if (!shell) shell = "/usr/local/bin/yaitso";
        char *argv[] = { shell, NULL };
        yt_session_new_tab(&g_sess, argv, g_cols, g_rows);
        g_need_redraw = 1;
        return;
    }
    if (sym == XKB_KEY_w && (mods & YWT_MOD_CTRL) &&
        (mods & YWT_MOD_SHIFT)) {
        if (g_sess.active >= 0) {
            yt_session_close_tab(&g_sess, g_sess.active);
            if (g_sess.ntabs == 0) g_closing = 1;
            g_need_redraw = 1;
        }
        return;
    }

    if ((mods & YWT_MOD_ALT) && sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int idx = (int)(sym - XKB_KEY_1);
        yt_session_switch(&g_sess, idx);
        g_need_redraw = 1;
        return;
    }

    if (sym == XKB_KEY_f && (mods & YWT_MOD_CTRL) &&
        (mods & YWT_MOD_SHIFT)) {
        yt_search_start(&g_search);
        g_need_redraw = 1;
        return;
    }

    if (g_search.active) {
        struct yt_tab *t = active_tab();
        if (!t) return;
        if (sym == XKB_KEY_Escape) { yt_search_stop(&g_search); g_need_redraw = 1; return; }
        if (sym == XKB_KEY_Return) {
            if (g_search.cur_match >= 0) t->term.scroll_off = 0;
            yt_search_stop(&g_search);
            g_need_redraw = 1;
            return;
        }
        if (sym == XKB_KEY_Down) { yt_search_next(&g_search, &t->term, +1); g_need_redraw = 1; return; }
        if (sym == XKB_KEY_Up)   { yt_search_next(&g_search, &t->term, -1); g_need_redraw = 1; return; }
        if (utf8 && utf8[0]) {
            uint32_t cp = (unsigned char)utf8[0];
            yt_search_input(&g_search, &t->term, cp);
            g_need_redraw = 1;
            return;
        }
        return;
    }

    if (sym == XKB_KEY_Page_Up && (mods & YWT_MOD_SHIFT)) {
        struct yt_tab *t = active_tab();
        if (t) yt_term_scroll_history(&t->term, +10);
        g_need_redraw = 1;
        return;
    }
    if (sym == XKB_KEY_Page_Down && (mods & YWT_MOD_SHIFT)) {
        struct yt_tab *t = active_tab();
        if (t) yt_term_scroll_history(&t->term, -10);
        g_need_redraw = 1;
        return;
    }
    {
        struct yt_tab *t = active_tab();
        if (t && t->term.scroll_off &&
            sym != XKB_KEY_Page_Up && sym != XKB_KEY_Page_Down)
            yt_term_scroll_history(&t->term, -t->term.scroll_off);
    }

    key_to_input(sym, mods, utf8);
}

static void on_axis(void *data, int x, int y, uint32_t axis, int32_t value)
{
    (void)data; (void)x; (void)y;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    struct yt_tab *t = active_tab();
    if (t) yt_term_scroll_history(&t->term, value > 0 ? +3 : -3);
    g_need_redraw = 1;
}

static void on_configure(void *data, int w, int h)
{
    (void)data;
    if (!g_surf) return;
    int cols = w / YT_CELL_W;
    int rows = h / YT_CELL_H;
    if (cols < 1)  cols = 1;
    if (rows < 1)  rows = 1;
    if (cols > YT_COLS_MAX) cols = YT_COLS_MAX;
    if (rows > YT_ROWS_MAX) rows = YT_ROWS_MAX;
    g_cols = cols;
    g_rows = rows;
    for (int i = 0; i < g_sess.ntabs; i++) {
        yt_term_resize(&g_sess.tabs[i].term, cols, rows);
        yt_pty_resize(g_sess.tabs[i].master_fd, cols, rows);
    }
    if (g_buf) ywt_buf_destroy(g_buf);
    g_buf = ywt_buf_create(g_surf, cols * YT_CELL_W, rows * YT_CELL_H);
    g_need_redraw = 1;
}

static void on_close(void *data) { (void)data; g_closing = 1; }

static void draw_tab_bar(ywt_buf_t *b, ywt_font_t *f)
{
    if (!g_show_tabs || g_sess.ntabs <= 1) return;
    int y = 0;
    for (int i = 0; i < g_sess.ntabs; i++) {
        struct yt_tab *t = &g_sess.tabs[i];
        uint32_t bg = (i == g_sess.active) ? 0xff3b4252 : 0xff2e3440;
        uint32_t fg = (i == g_sess.active) ? 0xff88c0d0 : 0xffd8dee9;
        if (t->has_bell) fg = 0xffebcb8b;
        int tw = ywt_text_width(f, t->title) + 16;
        ywt_draw_rect(b, y, 0, tw, YT_CELL_H, bg);
        ywt_draw_text(b, f, t->title, y + 8, 0, fg);
        if (i == g_sess.active)
            ywt_draw_rect(b, y, YT_CELL_H - 2, tw, 2, 0xff88c0d0);
        y += tw;
        t->has_bell = 0;
    }
}

static void draw_search(ywt_buf_t *b, ywt_font_t *f)
{
    if (!g_search.active) return;
    char buf[256];
    if (g_search.nmatch > 0)
        snprintf(buf, sizeof(buf), "/%s  [%d/%d]",
                 g_search.query, g_search.cur_match + 1, g_search.nmatch);
    else
        snprintf(buf, sizeof(buf), "/%s  [no match]", g_search.query);
    int w = ywt_text_width(f, buf) + 16;
    ywt_draw_rect(b, 0, ywt_buf_height(b) - YT_CELL_H - 4,
                  w, YT_CELL_H, 0xff3b4252);
    ywt_draw_text(b, f, buf, 8,
                  ywt_buf_height(b) - YT_CELL_H - 4, 0xffebcb8b);
}

static void render(void)
{
    if (!g_buf || !g_font) return;
    struct yt_tab *t = active_tab();
    if (!t) {
        ywt_buf_clear(g_buf, YT_COLOR_BG_DEF);
    } else {
        yt_term_render(&t->term, g_buf, g_font);
    }
    draw_tab_bar(g_buf, g_font);
    draw_search(g_buf, g_font);

    struct wl_buffer *wl = ywt_buf_wl(g_buf);
    wl_surface_attach(ywt_surface_wl(g_surf), wl, 0, 0);
    wl_surface_damage_buffer(ywt_surface_wl(g_surf), 0, 0,
                             ywt_buf_width(g_buf), ywt_buf_height(g_buf));
    ywt_surface_commit(g_surf);
}

int main(int argc, char **argv)
{
    const char *font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
    int font_size = 14;
    int no_tabs = 0;

    char *child_argv[16];
    int cn = 0;
    const char *shell = getenv("SHELL");
    if (!shell) shell = "/usr/local/bin/yaitso";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) font_path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) font_size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-tabs")) no_tabs = 1;
        else if (!strcmp(argv[i], "-e")) {
            cn = 0;
            for (int j = i + 1; j < argc && cn < 15; j++) child_argv[cn++] = argv[j];
            break;
        } else if (!strcmp(argv[i], "--test-color")) {
            ywt_ctx_t *ctx = ywt_ctx_create();
            if (!ctx) return 1;
            ywt_surface_t *s = ywt_surface_create(ctx, 640, 200, "yterm-color", "yterm");
            ywt_font_t *f = ywt_font_load(font_path, font_size);
            ywt_buf_t *b = ywt_buf_create(s, 640, 200);
            ywt_buf_clear(b, 0xff2e3440);
            const char *lines[] = {
                "16 colors:",
                "\\033[38;5;0m black \\033[38;5;1m red \\033[38;5;2m green \\033[38;5;3m yellow",
                "\\033[38;5;4m blue \\033[38;5;5m magenta \\033[38;5;6m cyan \\033[38;5;7m white",
                "\\033[38;2;255;0;0m 24-bit red \\033[38;2;0;255;0m green \\033[38;2;0;0;255m blue",
                NULL
            };
            for (int i = 0; lines[i]; i++)
                ywt_draw_text(b, f, lines[i], 8, 8 + i * 24, 0xffd8dee9);
            struct wl_buffer *wl = ywt_buf_wl(b);
            wl_surface_attach(ywt_surface_wl(s), wl, 0, 0);
            wl_surface_damage_buffer(ywt_surface_wl(s), 0, 0, 640, 200);
            ywt_surface_commit(s);
            ywt_ctx_roundtrip(ctx);
            sleep(3);
            ywt_buf_destroy(b);
            ywt_font_destroy(f);
            ywt_surface_destroy(s);
            ywt_ctx_destroy(ctx);
            return 0;
        }
    }

    if (cn == 0) { child_argv[0] = (char *)shell; child_argv[1] = NULL; cn = 1; }

    ywt_ctx_t *ctx = ywt_ctx_create();
    if (!ctx) { fprintf(stderr, "yterm: no display\n"); return 1; }

    g_surf = ywt_surface_create(ctx, 720, 400, "yterm", "yterm.yaitso");
    if (!g_surf) { ywt_ctx_destroy(ctx); return 1; }

    ywt_surface_set_cb(g_surf, NULL, on_key, NULL, NULL, on_axis,
                       on_close, on_configure, NULL);

    g_font = ywt_font_load(font_path, font_size);
    if (!g_font) g_font = ywt_font_load(
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf", font_size);

    yt_session_init(&g_sess, g_cols, g_rows);
    if (yt_session_new_tab(&g_sess, child_argv, g_cols, g_rows) < 0) {
        fprintf(stderr, "yterm: pty spawn failed\n");
        return 1;
    }
    for (int i = 0; i < g_sess.ntabs; i++)
        fcntl(g_sess.tabs[i].master_fd, F_SETFL, O_NONBLOCK);

    g_buf = ywt_buf_create(g_surf, g_cols * YT_CELL_W, g_rows * YT_CELL_H);
    if (!g_buf) { fprintf(stderr, "yterm: buf create failed\n"); return 1; }

    (void)no_tabs;

    int ep = epoll_create1(0);
    int wl_fd = ywt_ctx_fd(ctx);

    while (!g_closing) {
        ywt_ctx_dispatch_pending(ctx);
        ywt_ctx_flush(ctx);

        struct epoll_event ev[YT_MAX_TABS + 1];
        struct epoll_event reg;

        for (int i = 0; i < g_sess.ntabs; i++) {
            if (g_sess.tabs[i].master_fd < 0) continue;
            reg.events = EPOLLIN;
            reg.data.fd = g_sess.tabs[i].master_fd;
            epoll_ctl(ep, EPOLL_CTL_ADD, g_sess.tabs[i].master_fd, &reg);
        }
        reg.events = EPOLLIN;
        reg.data.fd = wl_fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, wl_fd, &reg);

        int n = epoll_wait(ep, ev, YT_MAX_TABS + 1, 100);

        for (int i = 0; i < g_sess.ntabs; i++) {
            if (g_sess.tabs[i].master_fd >= 0)
                epoll_ctl(ep, EPOLL_CTL_DEL, g_sess.tabs[i].master_fd, NULL);
        }
        epoll_ctl(ep, EPOLL_CTL_DEL, wl_fd, NULL);

        for (int k = 0; k < n; k++) {
            if (ev[k].data.fd == wl_fd) {
                if (ywt_ctx_dispatch(ctx) < 0) g_closing = 1;
                continue;
            }
            for (int i = 0; i < g_sess.ntabs; i++) {
                struct yt_tab *t = &g_sess.tabs[i];
                if (t->master_fd != ev[k].data.fd) continue;
                char buf[8192];
                ssize_t r = read(t->master_fd, buf, sizeof(buf));
                if (r > 0) {
                    yt_term_feed(&t->term, buf, (size_t)r);
                    if (t->term.bell) {
                        t->has_bell = 1;
                        t->term.bell = 0;
                        yt_bell_notify(t->title);
                    }
                    g_need_redraw = 1;
                } else if (r == 0) {
                    yt_session_close_tab(&g_sess, i);
                    if (g_sess.ntabs == 0) g_closing = 1;
                    g_need_redraw = 1;
                    break;
                }
            }
        }

        if (g_need_redraw && g_buf && g_surf && g_font) {
            render();
            g_need_redraw = 0;
        }
    }

    close(ep);
    for (int i = 0; i < g_sess.ntabs; i++) {
        if (g_sess.tabs[i].master_fd >= 0) close(g_sess.tabs[i].master_fd);
    }
    if (g_buf)  ywt_buf_destroy(g_buf);
    if (g_font) ywt_font_destroy(g_font);
    ywt_surface_destroy(g_surf);
    ywt_ctx_destroy(ctx);
    return 0;
}
