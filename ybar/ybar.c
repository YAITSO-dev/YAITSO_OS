#define _GNU_SOURCE
#include "ybar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>
#include <linux/input-event-codes.h>

static ywt_surface_t *g_surf = NULL;
static ywt_buf_t     *g_buf = NULL;
static ywt_font_t    *g_font = NULL;
static struct ybar_state g_st;
static int            g_w = 800;
static int            g_closing = 0;
static int            g_dirty = 1;

static int g_menu_x = 8, g_menu_w = 0;
static int g_win_x[YBAR_MAX_WIN];
static int g_win_w[YBAR_MAX_WIN];

static void refresh_windows(void)
{
    if (ybar_ipc_list(&g_st) != 0) {
        g_st.nwin = 0;
        g_st.focus_id = -1;
    }
}

static int draw_window_list(int x)
{
    for (int i = 0; i < g_st.nwin && x < g_w - 200; i++) {
        const char *title = g_st.wins[i][0] ? g_st.wins[i] : "window";
        int tw = ywt_text_width(g_font, title);
        int w = tw + 18;
        if (x + w > g_w - 180) break;

        g_win_x[i] = x;
        g_win_w[i] = w;

        int active = (g_st.win_id[i] == g_st.focus_id);
        if (active)
            ywt_draw_rect(g_buf, x, 4, w, YBAR_HEIGHT - 8, 0xff3b4252);
        if (g_st.win_hidden[i]) {
            ywt_draw_text(g_buf, g_font, title, x + 9, 7, 0xff6c7a8c);
        } else {
            ywt_draw_text(g_buf, g_font, title, x + 9, 7,
                          active ? 0xff88c0d0 : 0xffd8dee9);
        }
        x += w + 4;
    }
    return x;
}

static void draw(void)
{
    if (!g_buf || !g_font) return;
    if (!ywt_surface_is_configured(g_surf)) return;
    uint32_t bg = 0xff242a33;
    uint32_t fg = 0xffd8dee9;
    uint32_t muted = 0xff8b98a8;

    ywt_buf_clear(g_buf, bg);

    /* Кнопка меню/запуска приложений. */
    const char *menu = " APPS ";
    g_menu_w = ywt_text_width(g_font, menu) + 12;
    g_menu_x = 8;
    ywt_draw_rect(g_buf, g_menu_x, 5, g_menu_w, YBAR_HEIGHT - 10, 0xff3b4252);
    ywt_draw_text(g_buf, g_font, menu, g_menu_x + 6, 8, 0xff88c0d0);

    draw_window_list(g_menu_x + g_menu_w + 10);

    /* Правая часть: заряд, сеть, дата, часы. */
    int xr = g_w - 8;
    if (g_st.clock[0]) {
        int cw = ywt_text_width(g_font, g_st.clock);
        xr -= cw;
        ywt_draw_text(g_buf, g_font, g_st.clock, xr, 7, fg);
        xr -= 10;
    }
    if (g_st.date[0]) {
        int dw = ywt_text_width(g_font, g_st.date);
        xr -= dw;
        ywt_draw_text(g_buf, g_font, g_st.date, xr, 7, muted);
        xr -= 14;
    }
    if (g_st.has_battery) {
        char b[16];
        snprintf(b, sizeof(b), "%d%%", g_st.battery);
        int bw = ywt_text_width(g_font, b);
        xr -= bw;
        ywt_draw_text(g_buf, g_font, b, xr, 7, 0xffebcb8b);
        xr -= 14;
    }
    if (g_st.ip[0]) {
        int iw = ywt_text_width(g_font, g_st.ip);
        xr -= iw;
        ywt_draw_text(g_buf, g_font, g_st.ip, xr, 7, 0xffa3be8c);
    }

    struct wl_buffer *wl = ywt_buf_wl(g_buf);
    wl_surface_attach(ywt_surface_wl(g_surf), wl, 0, 0);
    wl_surface_damage_buffer(ywt_surface_wl(g_surf), 0, 0,
                             ywt_buf_width(g_buf), ywt_buf_height(g_buf));
    ywt_surface_commit(g_surf);
}

static void on_ptr(void *data, int x, int y, uint32_t button, uint32_t state)
{
    (void)data; (void)y;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (button != BTN_LEFT) return;

    char reply[128];
    if (x >= g_menu_x && x < g_menu_x + g_menu_w) {
        ybar_ipc_send("launcher", reply, sizeof(reply));
        return;
    }
    for (int i = 0; i < g_st.nwin; i++) {
        if (x >= g_win_x[i] && x < g_win_x[i] + g_win_w[i]) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "focus %d", g_st.win_id[i]);
            ybar_ipc_send(cmd, reply, sizeof(reply));
            refresh_windows();
            g_dirty = 1;
            return;
        }
    }
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

    ywt_surface_set_cb(g_surf, NULL, NULL, on_ptr, NULL, NULL,
                       on_close, on_configure, NULL);

    g_font = ywt_font_load(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12);
    if (!g_font) g_font = ywt_font_load(
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf", 12);

    g_buf = ywt_buf_create(g_surf, g_w, YBAR_HEIGHT);

    time_t last_clock = 0, last_ip = 0, last_win = 0;
    int ep = epoll_create1(0);
    int wl_fd = ywt_ctx_fd(ctx);

    while (!g_closing) {
        ywt_ctx_dispatch_pending(ctx);
        ywt_ctx_flush(ctx);

        time_t now = time(NULL);
        if (now - last_clock >= 10 || last_clock == 0) {
            struct tm tm;
            localtime_r(&now, &tm);
            strftime(g_st.clock, sizeof(g_st.clock), "%H:%M", &tm);
            strftime(g_st.date, sizeof(g_st.date), "%a %d %b", &tm);
            last_clock = now;
            g_dirty = 1;
        }
        if (now - last_ip >= 10 || last_ip == 0) {
            ybar_widgets_update(&g_st);
            last_ip = now;
            g_dirty = 1;
        }
        if (now - last_win >= 2 || last_win == 0) {
            refresh_windows();
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
