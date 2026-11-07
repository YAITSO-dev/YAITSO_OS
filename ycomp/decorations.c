#include "ycomp.h"
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_scene.h>
#include <stdlib.h>

struct ycomp_deco {
    struct wl_list              link;
    struct wlr_xdg_toplevel_decoration_v1 *deco;
    struct wl_listener           destroy;
    struct wl_listener           request_mode;
};

static void deco_request_mode(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_deco *dc = wl_container_of(l, dc, request_mode);
    wlr_xdg_toplevel_decoration_v1_set_mode(dc->deco,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void deco_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_deco *dc = wl_container_of(l, dc, destroy);
    wl_list_remove(&dc->destroy.link);
    wl_list_remove(&dc->request_mode.link);
    wl_list_remove(&dc->link);
    free(dc);
}

static struct wl_list g_decos;

static void deco_new(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    struct ycomp_deco *dc = calloc(1, sizeof(*dc));
    if (!dc) return;
    dc->deco = deco;
    dc->request_mode.notify = deco_request_mode;
    wl_signal_add(&deco->events.request_mode, &dc->request_mode);
    dc->destroy.notify = deco_destroy;
    wl_signal_add(&deco->events.destroy, &dc->destroy);
    wlr_xdg_toplevel_decoration_v1_set_mode(deco,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_list_insert(&g_decos, &dc->link);
}

static struct wl_listener g_deco_listener;

void ycomp_decorations_init(struct ycomp_server *s)
{
    wl_list_init(&g_decos);
    g_deco_listener.notify = deco_new;
    wl_signal_add(&s->deco_manager->events.new_toplevel_decoration,
                  &g_deco_listener);
}
