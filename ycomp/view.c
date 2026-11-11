#include "ycomp.h"
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <stdlib.h>
#include <string.h>

#define BAR_APP_ID "ybar.yaitso"

static int view_is_bar(struct ycomp_view *v)
{
    const char *app = v->xdg_toplevel->app_id;
    return app && !strcmp(app, BAR_APP_ID);
}

static void view_map(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, map);
    v->mapped = 1;
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(v->xdg_toplevel->base, &geo);
    if (v->w <= 0) v->w = geo.width;
    if (v->h <= 0) v->h = geo.height;

    struct wlr_box screen = {0};
    wlr_output_layout_get_box(v->server->output_layout, NULL, &screen);

    if (view_is_bar(v) && screen.width > 0) {
        /* ybar — панель: во всю ширину экрана у верхнего края. */
        v->x = 0;
        v->y = 0;
        v->w = screen.width;
        v->h = geo.height > 0 ? geo.height : 28;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    } else {
        v->x = 100 + (v->id % 8) * 40;
        v->y = 100 + (v->id % 8) * 40;
    }

    wlr_scene_node_set_position(&v->scene_tree->node, v->x, v->y);
    wlr_scene_node_set_enabled(&v->scene_tree->node, true);
    wlr_scene_node_raise_to_top(&v->scene_tree->node);
    ycomp_focus_view(v->server, v->id);
}

/* Передаёт клавиатурный фокус поверхности активного окна. */
static void keyboard_focus(struct ycomp_server *s, struct ycomp_view *v)
{
    if (!s->kb_group) return;
    wlr_seat_set_keyboard(s->seat, &s->kb_group->keyboard);
    if (v && v->mapped) {
        wlr_seat_keyboard_notify_enter(s->seat,
            v->xdg_toplevel->base->surface,
            s->kb_group->keyboard.keycodes,
            s->kb_group->keyboard.num_keycodes,
            &s->kb_group->keyboard.modifiers);
    } else {
        wlr_seat_keyboard_notify_clear_focus(s->seat);
    }
}

static void view_unmap(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, unmap);
    v->mapped = 0;
    if (v->server->focused_id == v->id) {
        v->server->focused_id = -1;
        keyboard_focus(v->server, NULL);
    }
}

/* Первый commit xdg_surface означает, что роль инициализирована — только
 * теперь wlroots может отправить configure с размером окна. */
static void view_first_commit(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, first_commit);
    wl_list_remove(&v->first_commit.link);
    wl_list_init(&v->first_commit.link);
    wlr_xdg_toplevel_set_size(v->xdg_toplevel, 800, 600);
}

static void view_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, destroy);
    if (v->server->focused_id == v->id) {
        v->server->focused_id = -1;
        keyboard_focus(v->server, NULL);
    }
    wl_list_remove(&v->map.link);
    wl_list_remove(&v->unmap.link);
    wl_list_remove(&v->destroy.link);
    wl_list_remove(&v->first_commit.link);
    wl_list_remove(&v->request_move.link);
    wl_list_remove(&v->request_resize.link);
    wl_list_remove(&v->request_maximize.link);
    wl_list_remove(&v->request_fullscreen.link);
    wl_list_remove(&v->set_title.link);
    wl_list_remove(&v->set_app_id.link);
    wl_list_remove(&v->link);
    free(v);
}

static void view_request_move(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_move);
    struct ycomp_server *s = v->server;
    s->grab_view = v;
    s->grabbing = 1;
    s->grab_resize = 0;
    s->grab_sx = s->cursor->x;
    s->grab_sy = s->cursor->y;
    s->grab_view_x = v->x;
    s->grab_view_y = v->y;
}

static void view_request_resize(struct wl_listener *l, void *data)
{
    struct ycomp_view *v = wl_container_of(l, v, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    struct ycomp_server *s = v->server;
    s->grab_view = v;
    s->grabbing = 1;
    s->grab_resize = 1;
    s->grab_button = (uint32_t)ev->edges;
    s->grab_sx = s->cursor->x;
    s->grab_sy = s->cursor->y;
    s->grab_view_w = v->w;
    s->grab_view_h = v->h;
}

static void view_request_maximize(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_maximize);
    wlr_xdg_toplevel_set_maximized(v->xdg_toplevel, true);
}

static void view_request_fullscreen(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_fullscreen);
    wlr_xdg_toplevel_set_fullscreen(v->xdg_toplevel, true);
}

static void view_set_title(struct wl_listener *l, void *data)
{
    (void)l; (void)data;
}

static void view_set_app_id(struct wl_listener *l, void *data)
{
    (void)l; (void)data;
}

void ycomp_xdg_new_toplevel(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_xdg_toplevel);
    struct wlr_xdg_toplevel *tl = data;

    struct ycomp_view *v = calloc(1, sizeof(*v));
    if (!v) return;
    v->server = s;
    v->xdg_toplevel = tl;
    v->id = s->next_id++;
    v->scene_tree = wlr_scene_xdg_surface_create(&s->scene->tree, tl->base);
    v->scene_tree->node.data = v;
    tl->base->data = v;
    wlr_scene_node_set_enabled(&v->scene_tree->node, false);

    v->map.notify = view_map;
    wl_signal_add(&tl->base->surface->events.map, &v->map);
    v->unmap.notify = view_unmap;
    wl_signal_add(&tl->base->surface->events.unmap, &v->unmap);
    v->destroy.notify = view_destroy;
    wl_signal_add(&tl->base->events.destroy, &v->destroy);
    v->first_commit.notify = view_first_commit;
    wl_signal_add(&tl->base->surface->events.commit, &v->first_commit);
    v->request_move.notify = view_request_move;
    wl_signal_add(&tl->events.request_move, &v->request_move);
    v->request_resize.notify = view_request_resize;
    wl_signal_add(&tl->events.request_resize, &v->request_resize);
    v->request_maximize.notify = view_request_maximize;
    wl_signal_add(&tl->events.request_maximize, &v->request_maximize);
    v->request_fullscreen.notify = view_request_fullscreen;
    wl_signal_add(&tl->events.request_fullscreen, &v->request_fullscreen);
    v->set_title.notify = view_set_title;
    wl_signal_add(&tl->events.set_title, &v->set_title);
    v->set_app_id.notify = view_set_app_id;
    wl_signal_add(&tl->events.set_app_id, &v->set_app_id);

    wl_list_insert(&s->views, &v->link);
}

void ycomp_xdg_new_popup(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;
    struct ycomp_view *v = popup->base->data;
    if (v && v->scene_tree)
        wlr_scene_xdg_surface_create(v->scene_tree, popup->base);
    else
        wlr_scene_xdg_surface_create(&s->scene->tree, popup->base);
}

void ycomp_focus_view(struct ycomp_server *s, int id)
{
    struct ycomp_view *v, *target = NULL;
    wl_list_for_each(v, &s->views, link) {
        int active = (v->id == id && v->mapped);
        v->focused = active;
        wlr_xdg_toplevel_set_activated(v->xdg_toplevel, active);
        if (active) target = v;
    }
    if (target) {
        wlr_scene_node_raise_to_top(&target->scene_tree->node);
        s->focused_id = id;
        keyboard_focus(s, target);
        /* Панель всегда остаётся поверх остальных окон. */
        wl_list_for_each(v, &s->views, link)
            if (v->mapped && view_is_bar(v))
                wlr_scene_node_raise_to_top(&v->scene_tree->node);
    } else {
        s->focused_id = -1;
        keyboard_focus(s, NULL);
    }
}

void ycomp_close_view(struct ycomp_server *s, int id)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (v->id == id && v->mapped) {
            wlr_xdg_toplevel_send_close(v->xdg_toplevel);
            return;
        }
    }
}

int ycomp_count_views(struct ycomp_server *s)
{
    int n = 0;
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link) if (v->mapped) n++;
    return n;
}

struct ycomp_view *ycomp_view_at(struct ycomp_server *s, double lx, double ly,
                                 struct wlr_surface **surface,
                                 double *sx, double *sy)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped) continue;
        double _sx, _sy;
        struct wlr_surface *surf =
            wlr_xdg_surface_surface_at(v->xdg_toplevel->base,
                                       lx - v->x, ly - v->y, &_sx, &_sy);
        if (surf) {
            *surface = surf;
            *sx = _sx;
            *sy = _sy;
            return v;
        }
    }
    return NULL;
}

