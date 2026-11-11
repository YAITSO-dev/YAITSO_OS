#include "ywt-internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t ver)
{
    ywt_ctx_t *c = data;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        uint32_t v = ver < 4 ? ver : 4;
        if (v < 1) v = 1;
        c->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
    }
    else if (!strcmp(iface, wl_shm_interface.name))
        c->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        uint32_t v = ver < 2 ? ver : 2;
        if (v < 1) v = 1;
        c->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, v);
    }
    else if (!strcmp(iface, wl_seat_interface.name)) {
        uint32_t v = ver < 5 ? ver : 5;
        if (v < 1) v = 1;
        c->seat = wl_registry_bind(reg, name, &wl_seat_interface, v);
        ywt_input_bind_seat(c);
    }
    else if (!strcmp(iface, wl_output_interface.name) && !c->output) {
        uint32_t v = ver < 4 ? ver : 4;
        if (v < 3) v = 3;
        c->output = wl_registry_bind(reg, name, &wl_output_interface, v);
        c->output_version = (int)v;
    }
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_remove,
};

static void output_geometry(void *data, struct wl_output *o,
                            int32_t x, int32_t y,
                            int32_t pw, int32_t ph,
                            int32_t subpixel, const char *make,
                            const char *model, int32_t transform)
{
    (void)data; (void)o; (void)x; (void)y;
    (void)pw; (void)ph; (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *o,
                        uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
    ywt_ctx_t *c = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        c->output_w = w;
        c->output_h = h;
    }
    (void)o; (void)refresh;
}

static void output_done(void *data, struct wl_output *o) { (void)data; (void)o; }
static void output_scale(void *data, struct wl_output *o, int32_t s) { (void)data; (void)o; (void)s; }

static const struct wl_output_listener output_listener_v3 = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static void output_name_v4(void *data, struct wl_output *o, const char *n)
{
    (void)data; (void)o; (void)n;
}

static void output_description_v4(void *data, struct wl_output *o, const char *d)
{
    (void)data; (void)o; (void)d;
}

static const struct wl_output_listener output_listener_v4 = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name_v4,
    .description = output_description_v4,
};

ywt_ctx_t *ywt_ctx_create(void)
{
    ywt_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->display = wl_display_connect(NULL);
    if (!c->display) { free(c); return NULL; }
    c->registry = wl_display_get_registry(c->display);
    wl_registry_add_listener(c->registry, &reg_listener, c);
    if (wl_display_roundtrip(c->display) < 0) { ywt_ctx_destroy(c); return NULL; }
    if (!c->compositor || !c->shm || !c->wm_base) { ywt_ctx_destroy(c); return NULL; }
    if (c->output) {
        if (c->output_version >= 4)
            wl_output_add_listener(c->output, &output_listener_v4, c);
        else
            wl_output_add_listener(c->output, &output_listener_v3, c);
        wl_display_roundtrip(c->display);
    }
    ywt_input_attach(c);
    return c;
}

void ywt_ctx_destroy(ywt_ctx_t *c)
{
    if (!c) return;
    ywt_input_detach(c);
    if (c->output)     wl_output_destroy(c->output);
    if (c->seat)       wl_seat_destroy(c->seat);
    if (c->wm_base)    xdg_wm_base_destroy(c->wm_base);
    if (c->shm)        wl_shm_destroy(c->shm);
    if (c->compositor) wl_compositor_destroy(c->compositor);
    if (c->registry)   wl_registry_destroy(c->registry);
    if (c->display)    wl_display_disconnect(c->display);
    free(c);
}

struct wl_display    *ywt_ctx_display(ywt_ctx_t *c)    { return c->display; }
int                   ywt_ctx_dispatch(ywt_ctx_t *c)   { return wl_display_dispatch(c->display); }
int                   ywt_ctx_dispatch_pending(ywt_ctx_t *c) { return wl_display_dispatch_pending(c->display); }
int                   ywt_ctx_flush(ywt_ctx_t *c)      { return wl_display_flush(c->display); }
int                   ywt_ctx_roundtrip(ywt_ctx_t *c)  { return wl_display_roundtrip(c->display); }
int                   ywt_ctx_fd(ywt_ctx_t *c)         { return wl_display_get_fd(c->display); }
struct wl_compositor *ywt_ctx_compositor(ywt_ctx_t *c) { return c->compositor; }
struct wl_shm        *ywt_ctx_shm(ywt_ctx_t *c)        { return c->shm; }
struct xdg_wm_base   *ywt_ctx_wm_base(ywt_ctx_t *c)    { return c->wm_base; }
struct wl_seat       *ywt_ctx_seat(ywt_ctx_t *c)       { return c->seat; }
int                   ywt_ctx_output_w(ywt_ctx_t *c)   { return c->output_w; }
int                   ywt_ctx_output_h(ywt_ctx_t *c)   { return c->output_h; }
