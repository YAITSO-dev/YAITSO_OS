#include "ylaunch.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <linux/input-event-codes.h>

static struct yl_app g_apps[YL_MAX_APPS];
static int            g_napps = 0;
static int            g_filtered[YL_MAX_APPS];
static int            g_nfiltered = 0;
static char           g_query[64] = "";
static int            g_sel = 0;
static int            g_closing = 0;
static int            g_dirty = 1;

static ywt_surface_t *g_surf = NULL;
static ywt_buf_t     *g_buf = NULL;
static ywt_font_t    *g_font = NULL;
static int            g_w = 640, g_h = 420;

#define ROW_H     22
#define HEADER_H  36
#define FOOTER_H  24

static int g_first = 0;

static int max_rows(void)
{
    int rows = (g_h - HEADER_H - FOOTER_H - 6) / ROW_H;
    return rows > 0 ? rows : 1;
}

static void refilter(void)
{
    g_nfiltered = 0;
    for (int i = 0; i < g_napps; i++) {
        if (!g_query[0] ||
            strcasestr(g_apps[i].name, g_query) ||
            strcasestr(g_apps[i].exec, g_query))
            g_filtered[g_nfiltered++] = i;
    }
    if (g_sel >= g_nfiltered) g_sel = g_nfiltered > 0 ? g_nfiltered - 1 : 0;
    if (g_sel < 0) g_sel = 0;
    g_dirty = 1;
}

static void draw(void)
{
    if (!g_buf || !g_font) return;
    if (!ywt_surface_is_configured(g_surf)) return;
    ywt_buf_clear(g_buf, 0xff21262e);
    ywt_draw_rect(g_buf, 0, 0, g_w, HEADER_H, 0xff2e3440);
    ywt_draw_rect(g_buf, 0, HEADER_H, g_w, 1, 0xff4c566a);

    char prompt[96];
    snprintf(prompt, sizeof(prompt), "> %s_", g_query);
    ywt_draw_text(g_buf, g_font, prompt, 10, 10, 0xffe5e9f0);

    int rows = max_rows();
    if (g_sel < g_first) g_first = g_sel;
    if (g_sel >= g_first + rows) g_first = g_sel - rows + 1;
    if (g_first < 0) g_first = 0;

    int y = HEADER_H + 6;
    for (int i = g_first; i < g_nfiltered && i < g_first + rows; i++) {
        int ai = g_filtered[i];
        if (i == g_sel) {
            ywt_draw_rect(g_buf, 0, y - 2, g_w, ROW_H, 0xff343c48);
            ywt_draw_rect(g_buf, 0, y - 2, 3, ROW_H, 0xff88c0d0);
        }
        uint32_t col = (i == g_sel) ? 0xff88c0d0 : 0xffd8dee9;
        ywt_draw_text(g_buf, g_font, g_apps[ai].name, 12, y, col);
        if (g_apps[ai].terminal)
            ywt_draw_text(g_buf, g_font, "[tty]", g_w - 52, y, 0xff8b98a8);
        y += ROW_H;
    }

    if (g_nfiltered == 0) {
        ywt_draw_text(g_buf, g_font, "Ничего не найдено", 12,
                      HEADER_H + 12, 0xff8b98a8);
    }

    ywt_draw_rect(g_buf, 0, g_h - FOOTER_H, g_w, 1, 0xff4c566a);
    char status[128];
    snprintf(status, sizeof(status), "%d из %d    Enter — запуск, Esc — закрыть",
             g_nfiltered, g_napps);
    ywt_draw_text(g_buf, g_font, status, 10, g_h - FOOTER_H + 5, 0xff8b98a8);

    struct wl_buffer *wl = ywt_buf_wl(g_buf);
    wl_surface_attach(ywt_surface_wl(g_surf), wl, 0, 0);
    wl_surface_damage_buffer(ywt_surface_wl(g_surf), 0, 0,
                             ywt_buf_width(g_buf), ywt_buf_height(g_buf));
    ywt_surface_commit(g_surf);
}

static int row_at(int y)
{
    if (y < HEADER_H || y >= g_h - FOOTER_H) return -1;
    int row = (y - HEADER_H - 6) / ROW_H;
    if (row < 0 || row >= max_rows()) return -1;
    int idx = g_first + row;
    return idx < g_nfiltered ? idx : -1;
}

static void on_ptr(void *data, int x, int y, uint32_t button, uint32_t state)
{
    (void)data; (void)x;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (button != BTN_LEFT) return;
    int idx = row_at(y);
    if (idx < 0) return;
    g_sel = idx;
    g_dirty = 1;
    yl_launch(&g_apps[g_filtered[g_sel]]);
    g_closing = 1;
}

static void on_motion(void *data, int x, int y)
{
    (void)data; (void)x;
    int idx = row_at(y);
    if (idx >= 0 && idx != g_sel) {
        g_sel = idx;
        g_dirty = 1;
    }
}

static void on_axis(void *data, int x, int y, uint32_t axis, int32_t value)
{
    (void)data; (void)x; (void)y;
    if (axis != 0) return;
    int step = value > 0 ? 3 : -3;
    g_sel += step;
    if (g_sel < 0) g_sel = 0;
    if (g_sel >= g_nfiltered) g_sel = g_nfiltered > 0 ? g_nfiltered - 1 : 0;
    g_dirty = 1;
}

static void on_key(void *data, xkb_keysym_t sym, uint32_t keycode,
                   uint32_t state, uint32_t mods, const char *utf8)
{
    (void)data; (void)keycode; (void)mods;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    if (sym == XKB_KEY_Escape) { g_closing = 1; return; }
    if (sym == XKB_KEY_Return) {
        if (g_nfiltered > 0) {
            yl_launch(&g_apps[g_filtered[g_sel]]);
            g_closing = 1;
        }
        return;
    }
    if (sym == XKB_KEY_Up)   { if (g_sel > 0) g_sel--; g_dirty = 1; return; }
    if (sym == XKB_KEY_Down) {
        if (g_sel + 1 < g_nfiltered) g_sel++;
        g_dirty = 1;
        return;
    }
    if (sym == XKB_KEY_Page_Up) {
        g_sel -= max_rows();
        if (g_sel < 0) g_sel = 0;
        g_dirty = 1;
        return;
    }
    if (sym == XKB_KEY_Page_Down) {
        g_sel += max_rows();
        if (g_sel >= g_nfiltered) g_sel = g_nfiltered > 0 ? g_nfiltered - 1 : 0;
        g_dirty = 1;
        return;
    }
    if (sym == XKB_KEY_Home) { g_sel = 0; g_dirty = 1; return; }
    if (sym == XKB_KEY_End)  {
        g_sel = g_nfiltered > 0 ? g_nfiltered - 1 : 0;
        g_dirty = 1;
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        size_t l = strlen(g_query);
        if (l) g_query[l - 1] = 0;
        refilter();
        return;
    }
    if (utf8 && utf8[0]) {
        size_t add = strlen(utf8);
        size_t l = strlen(g_query);
        if (l + add < sizeof(g_query) - 1) {
            memcpy(g_query + l, utf8, add);
            g_query[l + add] = 0;
            refilter();
        }
    }
}

static void on_configure(void *data, int w, int h)
{
    (void)data;
    if (w > 0) g_w = w;
    if (h > 0) g_h = h;
    if (g_buf) ywt_buf_destroy(g_buf);
    g_buf = ywt_buf_create(g_surf, g_w, g_h);
    g_dirty = 1;
}

static void on_close(void *data) { (void)data; g_closing = 1; }

int main(void)
{
    ywt_ctx_t *ctx = ywt_ctx_create();
    if (!ctx) { fprintf(stderr, "ylaunch: no display\n"); return 1; }

    g_surf = ywt_surface_create(ctx, g_w, g_h, "Приложения", "ylaunch.yaitso");
    if (!g_surf) { ywt_ctx_destroy(ctx); return 1; }

    ywt_surface_set_cb(g_surf, NULL, on_key, on_ptr, on_motion,
                       on_axis, on_close, on_configure, NULL);

    g_font = ywt_font_load(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14);
    if (!g_font) g_font = ywt_font_load(
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf", 14);

    g_buf = ywt_buf_create(g_surf, g_w, g_h);

    g_napps = yl_scan(g_apps, YL_MAX_APPS);
    refilter();

    int ep = epoll_create1(0);
    int wl_fd = ywt_ctx_fd(ctx);

    while (!g_closing) {
        ywt_ctx_dispatch_pending(ctx);
        ywt_ctx_flush(ctx);
        if (g_dirty) { draw(); g_dirty = 0; }

        struct epoll_event ev;
        struct epoll_event reg;
        reg.events = EPOLLIN;
        reg.data.fd = wl_fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, wl_fd, &reg);
        int n = epoll_wait(ep, &ev, 1, 100);
        epoll_ctl(ep, EPOLL_CTL_DEL, wl_fd, NULL);
        if (!g_closing && n > 0 && ev.data.fd == wl_fd) {
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
