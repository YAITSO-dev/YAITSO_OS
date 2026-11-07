#include "ybar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>

static ywt_surface_t *g_surf = NULL;
static ywt_buf_t     *g_buf = NULL;
static ywt_font_t    *g_font = NULL;
static struct ybar_state g_st;
static int            g_w = 800;
static int            g_closing = 0;
static int            g_dirty = 1;

static void draw(void)
{
    if (!g_buf || !g_font) return;
    uint32_t bg = 0xff2e3440;
    uint32_t fg = 0xffd8dee9;
    uint32_t accent = 0xff88c0d0;

    ywt_buf_clear(g_buf, bg);

    int x = 8;
    for (int i = 0; i < g_st.nwin && x < g_w / 2; i++) {
        uint32_t col = (i + 1 == g_st.focus_id) ? accent : fg;
        ywt_draw_text(g_buf, g_font, g_st.wins[i], x, 4, col);
        x += ywt_text_width(g_font, g_st.wins[i]) + 16;
    }

    int cw = ywt_text_width(g_font, g_st.clock);
    ywt_draw_text(g_buf, g_font, g_st.clock, g_w - cw - 8, 4, fg);

    if (g_st.has_battery) {
        char b[16];
        snprintf(b, sizeof(b), "%d%%", g_st.battery);
        int bw = ywt_text_width(g_font, b);
        ywt_draw_text(g_buf, g_font, b, g_w - cw - bw - 24, 4, 0xffebcb8b);
    }

    if (g_st.ip[0]) {
        int iw = ywt_text_width(g_font, g_st.ip);
        ywt_draw_text(g_buf, g_font, g_st.ip, g_w - cw - iw - 72, 4, 0xffa3be8c);
    }

    struct wl_buffer *wl = ywt_buf_wl(g_buf);
    wl_surface_attach(ywt_surface_wl(g_surf), wl, 0, 0);
    wl_surface_damage_buffer(ywt_surface_wl(g_surf), 0, 0,
                             ywt_buf_width(g_buf), ywt_buf_height(g_buf));
    ywt_surface_commit(g_surf);
}

static void on_configure(void *data, int w, int h)
{
    (void)data; (void)h;
    if (w <= 0) return;
    if (w != g_w || !g_buf) {
        g_w = w;
        if (g_buf) ywt_buf_destroy(g_buf);
        g_buf = ywt_buf_create(g_surf, g_w, YBAR_HEIGHT);
    }
    g_dirty = 1;
}

static void on_close(void *data) { (void)data; g_closing = 1; }

int main(void)
{
    ywt_ctx_t *ctx = ywt_ctx_create();
    if (!ctx) { fprintf(stderr, "ybar: no display\n"); return 1; }

    int w0 = ywt_ctx_output_w(ctx);
    if (w0 <= 0) w0 = 800;
    g_w = w0;

    g_surf = ywt_surface_create(ctx, g_w, YBAR_HEIGHT, "ybar", "ybar.yaitso");
    if (!g_surf) { ywt_ctx_destroy(ctx); return 1; }

    ywt_surface_set_cb(g_surf, NULL, NULL, NULL, NULL, NULL,
                       on_close, on_configure, NULL);

    g_font = ywt_font_load(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12);
    if (!g_font) g_font = ywt_font_load(
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf", 12);

    g_buf = ywt_buf_create(g_surf, g_w, YBAR_HEIGHT);

    time_t last_clock = 0, last_ip = 0, last_bat = 0, last_win = 0;
    int ep = epoll_create1(0);
    int wl_fd = ywt_ctx_fd(ctx);

    while (!g_closing) {
        ywt_ctx_dispatch_pending(ctx);
        ywt_ctx_flush(ctx);

        time_t now = time(NULL);
        if (now - last_clock >= 30 || last_clock == 0) {
            struct tm tm;
            localtime_r(&now, &tm);
            strftime(g_st.clock, sizeof(g_st.clock), "%H:%M", &tm);
            last_clock = now;
            g_dirty = 1;
        }
        if (now - last_ip >= 10 || last_ip == 0) {
            ybar_widgets_update(&g_st);
            last_ip = now;
            g_dirty = 1;
        }
        if (now - last_bat >= 60 || last_bat == 0) {
            ybar_widgets_update(&g_st);
            last_bat = now;
            g_dirty = 1;
        }
        if (now - last_win >= 2 || last_win == 0) {
            g_st.nwin = ybar_ipc_list(g_st.wins, YBAR_MAX_WIN);
            last_win = now;
            g_dirty = 1;
        }

        if (g_dirty) { draw(); g_dirty = 0; }

        struct epoll_event ev;
        struct epoll_event reg;
        reg.events = EPOLLIN;
        reg.data.fd = wl_fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, wl_fd, &reg);
        int n = epoll_wait(ep, &ev, 1, 200);
        epoll_ctl(ep, EPOLL_CTL_DEL, wl_fd, NULL);
        if (n > 0 && ev.data.fd == wl_fd) {
            if (ywt_ctx_dispatch(ctx) < 0) break;
        }
    }

    close(ep);
    if (g_buf)  ywt_buf_destroy(g_buf);
    if (g_font) ywt_font_destroy(g_font);
    ywt_surface_destroy(g_surf);
    ywt_ctx_destroy(ctx);
    return 0;
}
