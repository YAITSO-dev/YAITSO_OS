#include "ycomp.h"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <stdlib.h>

struct ycomp_layer {
    struct wl_list              link;
    struct ycomp_server        *server;
    struct wlr_layer_surface_v1 *ls;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wl_listener          map;
    struct wl_listener          unmap;
    struct wl_listener          destroy;
    struct wl_listener          new_popup;
};

static void layer_configure(struct ycomp_layer *ly)
{
    struct wlr_box full_area;
    wlr_output_layout_get_box(ly->server->output_layout, NULL, &full_area);
    struct wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(ly->scene, &full_area, &usable_area);
}

static void layer_map(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_layer *ly = wl_container_of(l, ly, map);
    wlr_scene_node_set_enabled(&ly->scene->tree->node, true);
    layer_configure(ly);
}

static void layer_unmap(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_layer *ly = wl_container_of(l, ly, unmap);
    wlr_scene_node_set_enabled(&ly->scene->tree->node, false);
}

static void layer_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_layer *ly = wl_container_of(l, ly, destroy);
    wl_list_remove(&ly->map.link);
    wl_list_remove(&ly->unmap.link);
    wl_list_remove(&ly->destroy.link);
    wl_list_remove(&ly->new_popup.link);
    wl_list_remove(&ly->link);
    free(ly);
}

static void layer_new_popup(struct wl_listener *l, void *data)
{
    (void)l; (void)data;
}

void ycomp_layer_new(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_layer_surface);
    struct wlr_layer_surface_v1 *ls = data;

    struct ycomp_layer *ly = calloc(1, sizeof(*ly));
    if (!ly) return;
    ly->server = s;
    ly->ls = ls;
    ly->scene = wlr_scene_layer_surface_v1_create(&s->scene->tree, ls);
    ly->scene->tree->node.data = ly;

    ly->map.notify = layer_map;
    wl_signal_add(&ls->surface->events.map, &ly->map);
    ly->unmap.notify = layer_unmap;
    wl_signal_add(&ls->surface->events.unmap, &ly->unmap);
    ly->destroy.notify = layer_destroy;
    wl_signal_add(&ls->events.destroy, &ly->destroy);
    ly->new_popup.notify = layer_new_popup;
    wl_signal_add(&ls->events.new_popup, &ly->new_popup);

    wl_list_insert(&s->views, &ly->link);
}
