#include "ywt-internal.h"
#include <stdlib.h>
#include <string.h>

struct wl_surface   *ywt_surface_wl(ywt_surface_t *s) { return s ? s->wl : NULL; }
ywt_ctx_t           *ywt_surface_ctx(ywt_surface_t *s) { return s ? s->ctx : NULL; }
int                  ywt_surface_width(ywt_surface_t *s)  { return s ? s->w : 0; }
int                  ywt_surface_height(ywt_surface_t *s) { return s ? s->h : 0; }
int                  ywt_surface_should_close(ywt_surface_t *s) { return s ? s->closed : 1; }

static void xdg_wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = {
    .ping = xdg_wm_ping,
};

static void xdg_surf_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    ywt_surface_t *s = d;
    xdg_surface_ack_configure(xs, serial);

    if (s->pending_w > 0 && s->pending_w != s->w) s->w = s->pending_w;
    if (s->pending_h > 0 && s->pending_h != s->h) s->h = s->pending_h;
    s->configured = 1;

    if (s->on_configure) s->on_configure(s->cb_data, s->w, s->h);
}

static const struct xdg_surface_listener xdg_surf_listener = {
    .configure = xdg_surf_configure,
};

static void top_configure(void *d, struct xdg_toplevel *t,
                          int32_t w, int32_t h, struct wl_array *states)
{
    (void)t; (void)states;
    ywt_surface_t *s = d;
    if (w > 0) s->pending_w = w;
    if (h > 0) s->pending_h = h;
}

static void top_close(void *d, struct xdg_toplevel *t)
{
    (void)t;
    ywt_surface_t *s = d;
    s->closed = 1;
    if (s->on_close) s->on_close(s->cb_data);
}

static void top_configure_bounds(void *d, struct xdg_toplevel *t,
                                 int32_t w, int32_t h)
{
    (void)d; (void)t; (void)w; (void)h;
}

static void top_wm_capabilities(void *d, struct xdg_toplevel *t,
                                struct wl_array *caps)
{
    (void)d; (void)t; (void)caps;
}

static const struct xdg_toplevel_listener top_listener = {
    .configure = top_configure,
    .close = top_close,
    .configure_bounds = top_configure_bounds,
    .wm_capabilities = top_wm_capabilities,
};

ywt_surface_t *ywt_surface_create(ywt_ctx_t *c, int w, int h,
                                  const char *title, const char *app_id)
{
    if (!c) return NULL;
    ywt_surface_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = c;
    s->w = w > 0 ? w : 640;
    s->h = h > 0 ? h : 480;

    s->wl = wl_compositor_create_surface(ywt_ctx_compositor(c));
    if (!s->wl) { free(s); return NULL; }
    wl_surface_set_user_data(s->wl, s);
    ywt_input_set_active(s);

    struct xdg_wm_base *wm = ywt_ctx_wm_base(c);
    xdg_wm_base_add_listener(wm, &wm_listener, NULL);
    s->xdg = xdg_wm_base_get_xdg_surface(wm, s->wl);
    xdg_surface_add_listener(s->xdg, &xdg_surf_listener, s);
    s->top = xdg_surface_get_toplevel(s->xdg);
    xdg_toplevel_add_listener(s->top, &top_listener, s);
    if (title)  xdg_toplevel_set_title(s->top, title);
    if (app_id) xdg_toplevel_set_app_id(s->top, app_id);

    wl_surface_commit(s->wl);
    wl_display_roundtrip(ywt_ctx_display(c));
    return s;
}

void ywt_surface_destroy(ywt_surface_t *s)
{
    if (!s) return;
    ywt_input_set_active(NULL);
    if (s->top)  xdg_toplevel_destroy(s->top);
    if (s->xdg)  xdg_surface_destroy(s->xdg);
    if (s->wl)   wl_surface_destroy(s->wl);
    free(s);
}

void ywt_surface_set_cb(ywt_surface_t *s, void *data,
                        ywt_key_cb k, ywt_pointer_cb p,
                        ywt_motion_cb m, ywt_axis_cb a,
                        ywt_close_cb c, ywt_configure_cb cf,
                        ywt_frame_cb fr)
{
    if (!s) return;
    s->cb_data      = data;
    s->on_key       = k;
    s->on_ptr       = p;
    s->on_motion    = m;
    s->on_axis      = a;
    s->on_close     = c;
    s->on_configure = cf;
    s->on_frame     = fr;
}

void ywt_surface_commit(ywt_surface_t *s)
{
    if (!s) return;
    wl_surface_commit(s->wl);
}
