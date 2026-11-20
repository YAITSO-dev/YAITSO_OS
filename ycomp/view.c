#include "ycomp.h"
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <stdlib.h>
#include <string.h>

#define BAR_APP_ID "ybar.yaitso"

static const float TB_ACTIVE[4]   = {0.26f, 0.32f, 0.39f, 1.0f};
static const float TB_INACTIVE[4] = {0.16f, 0.19f, 0.23f, 1.0f};
static const float BTN_CLOSE[4]   = {0.74f, 0.33f, 0.34f, 1.0f};
static const float BTN_MAX[4]     = {0.42f, 0.60f, 0.68f, 1.0f};
static const float BTN_MIN[4]     = {0.82f, 0.67f, 0.34f, 1.0f};
static const float BTN_DIM[4]     = {0.25f, 0.28f, 0.32f, 1.0f};

int ycomp_view_is_bar(struct ycomp_view *v)
{
    const char *app = v->xdg_toplevel->app_id;
    return app && !strcmp(app, BAR_APP_ID);
}

static int view_is_launcher(struct ycomp_view *v)
{
    const char *app = v->xdg_toplevel->app_id;
    return app && !strcmp(app, "ylaunch.yaitso");
}

void ycomp_usable_area(struct ycomp_server *s, struct wlr_box *out)
{
    struct wlr_box screen = {0};
    wlr_output_layout_get_box(s->output_layout, NULL, &screen);
    out->x = screen.x;
    out->y = screen.y + s->bar_height;
    out->width = screen.width;
    out->height = screen.height - s->bar_height;
    if (out->height < 1) out->height = screen.height;
}

static void clamp_geometry(struct ycomp_server *s, struct ycomp_view *v)
{
    struct wlr_box u;
    ycomp_usable_area(s, &u);
    if (v->titlebar_h > 0 && v->titlebar_h < YCOMP_TITLEBAR)
        v->titlebar_h = YCOMP_TITLEBAR;
    if (v->w < 160) v->w = 160;
    if (v->h < 80)  v->h = 80;
    if (v->w > u.width)  v->w = u.width;
    int maxh = u.height - v->titlebar_h;
    if (maxh < 80) maxh = 80;
    if (v->h > maxh) v->h = maxh;
    int min_y = u.y + v->titlebar_h + 2;
    if (v->y < min_y) v->y = min_y;
    if (v->y > u.y + u.height - 40) v->y = u.y + u.height - 40;
    if (v->x > u.x + u.width - 80) v->x = u.x + u.width - 80;
    if (v->x < u.x - v->w + 80) v->x = u.x - v->w + 80;
}

void ycomp_view_sync_geometry(struct ycomp_view *v)
{
    struct ycomp_server *s = v->server;
    if (!v->container || !v->scene_tree) return;

    wlr_scene_node_set_position(&v->container->node, v->x, v->y);
    wlr_scene_node_set_position(&v->scene_tree->node, 0, v->titlebar_h);

    int show_tb = v->titlebar_h > 0 && v->mapped && !ycomp_view_is_bar(v);
    if (v->titlebar) {
        wlr_scene_node_set_enabled(&v->titlebar->node, show_tb);
        wlr_scene_node_set_position(&v->titlebar->node, 0, 0);
        wlr_scene_rect_set_size(v->titlebar, v->w > 0 ? v->w : 1,
                                v->titlebar_h > 0 ? v->titlebar_h : 1);
        wlr_scene_rect_set_color(v->titlebar,
            v->focused ? TB_ACTIVE : TB_INACTIVE);
    }
    if (v->btn_close) {
        int bw = 18, bh = 16;
        int bx = v->w - bw - 8;
        wlr_scene_node_set_enabled(&v->btn_close->node, show_tb);
        wlr_scene_node_set_position(&v->btn_close->node, bx,
                                    (v->titlebar_h - bh) / 2);
        wlr_scene_rect_set_size(v->btn_close, bw, bh);
        wlr_scene_rect_set_color(v->btn_close,
            v->focused ? BTN_CLOSE : BTN_DIM);
    }
    if (v->btn_max) {
        int bw = 18, bh = 16;
        int bx = v->w - 54;
        if (bx < 8) bx = 8;
        wlr_scene_node_set_enabled(&v->btn_max->node, show_tb);
        wlr_scene_node_set_position(&v->btn_max->node, bx,
                                    (v->titlebar_h - bh) / 2);
        wlr_scene_rect_set_size(v->btn_max, bw, bh);
        wlr_scene_rect_set_color(v->btn_max,
            v->focused ? BTN_MAX : BTN_DIM);
    }
    if (v->btn_min) {
        int bw = 18, bh = 16;
        int bx = v->w - 82;
        if (bx < 8) bx = 8;
        wlr_scene_node_set_enabled(&v->btn_min->node, show_tb);
        wlr_scene_node_set_position(&v->btn_min->node, bx,
                                    (v->titlebar_h - bh) / 2);
        wlr_scene_rect_set_size(v->btn_min, bw, bh);
        wlr_scene_rect_set_color(v->btn_min,
            v->focused ? BTN_MIN : BTN_DIM);
    }
    (void)s;
}

static void view_save_geometry(struct ycomp_view *v)
{
    v->saved_x = v->x;
    v->saved_y = v->y;
    v->saved_w = v->w;
    v->saved_h = v->h;
    v->saved_valid = 1;
}

static void view_restore_geometry(struct ycomp_view *v)
{
    if (!v->saved_valid) return;
    v->x = v->saved_x;
    v->y = v->saved_y;
    v->w = v->saved_w;
    v->h = v->saved_h;
    clamp_geometry(v->server, v);
    wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
}

static void view_layout_maximized(struct ycomp_server *s, struct ycomp_view *v)
{
    struct wlr_box u;
    ycomp_usable_area(s, &u);
    v->x = u.x;
    v->y = u.y;
    v->w = u.width;
    v->h = u.height - v->titlebar_h;
    if (v->h < 80) v->h = 80;
    wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    ycomp_view_sync_geometry(v);
}

void ycomp_view_set_maximized(struct ycomp_server *s, int id, int on)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link)
        if (v->id == id) break;
    if (!v || v->id != id || !v->mapped || ycomp_view_is_bar(v)) return;

    if (on) {
        if (!v->maximized) view_save_geometry(v);
        v->maximized = 1;
        wlr_xdg_toplevel_set_maximized(v->xdg_toplevel, true);
        view_layout_maximized(s, v);
    } else {
        v->maximized = 0;
        wlr_xdg_toplevel_set_maximized(v->xdg_toplevel, false);
        if (v->saved_valid) view_restore_geometry(v);
        ycomp_view_sync_geometry(v);
    }
    ycomp_focus_view(s, id);
}

void ycomp_view_toggle_maximized(struct ycomp_server *s, int id)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link)
        if (v->id == id) break;
    if (!v || v->id != id) return;
    ycomp_view_set_maximized(s, id, !v->maximized);
}

void ycomp_view_snap(struct ycomp_server *s, int id, int side)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link)
        if (v->id == id) break;
    if (!v || v->id != id || !v->mapped || ycomp_view_is_bar(v)) return;

    struct wlr_box u;
    ycomp_usable_area(s, &u);
    if (!v->maximized) view_save_geometry(v);
    v->maximized = 0;
    wlr_xdg_toplevel_set_maximized(v->xdg_toplevel, false);
    v->x = u.x + (side == 0 ? 0 : u.width / 2);
    v->y = u.y;
    v->w = u.width / 2;
    v->h = u.height - v->titlebar_h;
    if (v->h < 80) v->h = 80;
    wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    ycomp_view_sync_geometry(v);
    ycomp_focus_view(s, id);
}

void ycomp_view_minimize(struct ycomp_server *s, int id, int on)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link)
        if (v->id == id) break;
    if (!v || v->id != id || !v->mapped || ycomp_view_is_bar(v)) return;

    if (on) {
        if (v->hidden) return;
        v->hidden = 1;
        wlr_scene_node_set_enabled(&v->container->node, false);
        if (s->focused_id == id) {
            s->focused_id = -1;
            ycomp_focus_next(s, 1);
            if (s->focused_id < 0) {
                wlr_seat_keyboard_notify_clear_focus(s->seat);
            }
        }
    } else {
        if (!v->hidden) { ycomp_focus_view(s, id); return; }
        v->hidden = 0;
        wlr_scene_node_set_enabled(&v->container->node, true);
        ycomp_focus_view(s, id);
    }
}

static void view_map(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, map);
    struct ycomp_server *s = v->server;

    v->mapped = 1;
    v->hidden = 0;
    v->titlebar_h = YCOMP_TITLEBAR;

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(v->xdg_toplevel->base, &geo);
    if (v->w <= 0) v->w = geo.width;
    if (v->h <= 0) v->h = geo.height;

    struct wlr_box screen = {0};
    wlr_output_layout_get_box(s->output_layout, NULL, &screen);
    struct wlr_box usable;
    ycomp_usable_area(s, &usable);

    if (ycomp_view_is_bar(v) && screen.width > 0) {
        v->x = 0;
        v->y = 0;
        v->w = screen.width;
        v->h = geo.height > 0 ? geo.height : YCOMP_BAR_DEF;
        v->titlebar_h = 0;
        s->bar_height = v->h;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    } else if (view_is_launcher(v) && screen.width > 0) {
        v->titlebar_h = 0;
        v->w = screen.width > 720 ? 640 : screen.width - 80;
        v->h = usable.height > 520 ? 420 : usable.height - 80;
        if (v->w < 320) v->w = 320;
        if (v->h < 200) v->h = 200;
        v->x = usable.x + (usable.width - v->w) / 2;
        v->y = usable.y + (usable.height - v->h) / 2;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    } else {
        if (v->w <= 0) v->w = 800;
        if (v->h <= 0) v->h = 600;
        v->x = usable.x + 70 + (v->id % 7) * 36;
        v->y = usable.y + 46 + (v->id % 7) * 30;
        clamp_geometry(s, v);
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, v->w, v->h);
    }

    wlr_scene_node_set_enabled(&v->container->node, true);
    ycomp_view_sync_geometry(v);
    wlr_scene_node_raise_to_top(&v->container->node);
    /* Панель не участвует в фокусе: иначе её появление сбрасывало бы фокус
     * уже открытого окна. */
    if (!ycomp_view_is_bar(v))
        ycomp_focus_view(s, v->id);

    /* Панель могла появиться после окон — пересчитываем maximized-окна. */
    if (ycomp_view_is_bar(v)) {
        struct ycomp_view *w;
        wl_list_for_each(w, &s->views, link)
            if (w->mapped && w->maximized && !ycomp_view_is_bar(w))
                view_layout_maximized(s, w);
    }
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
    v->hidden = 0;
    if (v->server->focused_id == v->id) {
        v->server->focused_id = -1;
        keyboard_focus(v->server, NULL);
        ycomp_focus_next(v->server, 1);
    }
}

/* Первый commit xdg_surface означает, что роль инициализирована — только
 * теперь wlroots может отправить configure с размером окна. Configure
 * обязателен для всех поверхностей: без него клиент не вправе прикреплять
 * буфер и получает protocol error «xdg_surface has never been configured». */
static void view_first_commit(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, first_commit);
    wl_list_remove(&v->first_commit.link);
    wl_list_init(&v->first_commit.link);

    struct wlr_box screen = {0};
    wlr_output_layout_get_box(v->server->output_layout, NULL, &screen);

    if (ycomp_view_is_bar(v)) {
        int sw = screen.width > 0 ? screen.width : 800;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, sw, YCOMP_BAR_DEF);
    } else if (view_is_launcher(v)) {
        int sw = screen.width > 0 ? screen.width : 800;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel,
                                  sw > 720 ? 640 : sw - 80, 420);
    } else {
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, 800, 600);
    }
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
    if (v->container)
        wlr_scene_node_destroy(&v->container->node);
    free(v);
}

static void view_request_move(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_move);
    struct ycomp_server *s = v->server;
    if (v->maximized) return;
    s->grab_view = v;
    s->grabbing = 1;
    s->grab_resize = 0;
    s->grab_sx = s->cursor->x;
    s->grab_sy = s->cursor->y;
    s->grab_view_x = v->x;
    s->grab_view_y = v->y;
    s->grab_view_w = v->w;
    s->grab_view_h = v->h;
}

static void view_request_resize(struct wl_listener *l, void *data)
{
    struct ycomp_view *v = wl_container_of(l, v, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    struct ycomp_server *s = v->server;
    if (v->maximized) return;
    s->grab_view = v;
    s->grabbing = 1;
    s->grab_resize = 1;
    s->grab_edges = ev->edges;
    s->grab_sx = s->cursor->x;
    s->grab_sy = s->cursor->y;
    s->grab_view_x = v->x;
    s->grab_view_y = v->y;
    s->grab_view_w = v->w;
    s->grab_view_h = v->h;
}

static void view_request_maximize(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_maximize);
    ycomp_view_set_maximized(v->server, v->id, 1);
}

static void view_request_fullscreen(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_view *v = wl_container_of(l, v, request_fullscreen);
    /* Полноэкранный режим пока сводится к maximize: панель остаётся
     * доступной, а поведение клиента предсказуемым. */
    ycomp_view_set_maximized(v->server, v->id, 1);
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
    v->titlebar_h = YCOMP_TITLEBAR;

    v->container = wlr_scene_tree_create(&s->scene->tree);
    if (!v->container) { free(v); return; }
    v->container->node.data = v;
    v->scene_tree = wlr_scene_xdg_surface_create(v->container, tl->base);
    if (!v->scene_tree) {
        wlr_scene_node_destroy(&v->container->node);
        free(v);
        return;
    }
    tl->base->data = v;

    v->titlebar = wlr_scene_rect_create(v->container, 1, YCOMP_TITLEBAR,
                                        TB_INACTIVE);
    v->btn_close = wlr_scene_rect_create(v->container, 18, 16, BTN_DIM);
    v->btn_max = wlr_scene_rect_create(v->container, 18, 16, BTN_DIM);
    v->btn_min = wlr_scene_rect_create(v->container, 18, 16, BTN_DIM);
    wlr_scene_node_set_enabled(&v->titlebar->node, false);
    wlr_scene_node_set_enabled(&v->btn_close->node, false);
    wlr_scene_node_set_enabled(&v->btn_max->node, false);
    wlr_scene_node_set_enabled(&v->btn_min->node, false);

    wlr_scene_node_set_position(&v->scene_tree->node, 0, v->titlebar_h);
    wlr_scene_node_set_enabled(&v->container->node, false);

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
        if (v->id == id && v->mapped && !ycomp_view_is_bar(v)) {
            target = v;
            break;
        }
    }
    if (target && target->hidden) {
        target->hidden = 0;
        wlr_scene_node_set_enabled(&target->container->node, true);
    }

    int old = s->focused_id;
    wl_list_for_each(v, &s->views, link) {
        int active = target && v == target;
        v->focused = active;
        wlr_xdg_toplevel_set_activated(v->xdg_toplevel, active);
        if (v->mapped) ycomp_view_sync_geometry(v);
    }
    if (target) {
        if (old != id) s->previous_id = old;
        wlr_scene_node_raise_to_top(&target->container->node);
        s->focused_id = id;
        keyboard_focus(s, target);
        /* Панель всегда остаётся поверх остальных окон. */
        wl_list_for_each(v, &s->views, link)
            if (v->mapped && ycomp_view_is_bar(v) && !v->hidden)
                wlr_scene_node_raise_to_top(&v->container->node);
    } else {
        s->focused_id = -1;
        keyboard_focus(s, NULL);
    }
}

void ycomp_focus_next(struct ycomp_server *s, int dir)
{
    struct ycomp_view *v;
    struct ycomp_view *list[64];
    int n = 0;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped || v->hidden || ycomp_view_is_bar(v)) continue;
        if (n < (int)(sizeof(list) / sizeof(list[0]))) list[n++] = v;
    }
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++)
        if (list[i]->id == s->focused_id) { cur = i; break; }
    int next;
    if (cur < 0) next = dir > 0 ? 0 : n - 1;
    else next = (cur + (dir > 0 ? 1 : n - 1)) % n;
    ycomp_focus_view(s, list[next]->id);
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
    wl_list_for_each(v, &s->views, link)
        if (v->mapped && !ycomp_view_is_bar(v)) n++;
    return n;
}

struct ycomp_view *ycomp_view_at(struct ycomp_server *s, double lx, double ly,
                                 struct wlr_surface **surface,
                                 double *sx, double *sy)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped || v->hidden) continue;
        if (ly < v->y + v->titlebar_h) continue;
        double _sx, _sy;
        struct wlr_surface *surf =
            wlr_xdg_surface_surface_at(v->xdg_toplevel->base,
                                       lx - v->x,
                                       ly - v->y - v->titlebar_h,
                                       &_sx, &_sy);
        if (surf) {
            *surface = surf;
            *sx = _sx;
            *sy = _sy;
            return v;
        }
    }
    return NULL;
}

/* Возвращает окно под курсором, если курсор в его заголовке.
 * zone: 0 — тело заголовка, 1 — «закрыть», 2 — «развернуть». */
struct ycomp_view *ycomp_view_at_titlebar(struct ycomp_server *s, double lx,
                                          double ly, int *zone)
{
    struct ycomp_view *v, *target = NULL;
    for (int pass = 0; pass < 2 && !target; pass++) {
        wl_list_for_each(v, &s->views, link) {
            if (!v->mapped || v->hidden || v->titlebar_h <= 0) continue;
            if (ycomp_view_is_bar(v)) continue;
            if (pass == 0 && !v->focused) continue;
            if (lx >= v->x && lx < v->x + v->w &&
                ly >= v->y && ly < v->y + v->titlebar_h) {
                target = v;
                break;
            }
        }
    }
    if (!target) return NULL;
    if (zone) {
        if (lx >= target->x + target->w - 30) *zone = 1;
        else if (lx >= target->x + target->w - 58) *zone = 2;
        else if (lx >= target->x + target->w - 86) *zone = 3;
        else *zone = 0;
    }
    return target;
}
