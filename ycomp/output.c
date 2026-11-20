#include "ycomp.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static void output_frame(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_output *o = wl_container_of(l, o, frame);
    struct wlr_scene_output *so =
        wlr_scene_get_scene_output(o->server->scene, o->wlr_output);
    if (so) wlr_scene_output_commit(so, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (so) wlr_scene_output_send_frame_done(so, &now);
}

/* Мягкий вертикальный градиент вместо чёрного фона. */
static void output_bg_build(struct ycomp_output *o)
{
    struct ycomp_server *s = o->server;
    struct wlr_output *out = o->wlr_output;
    int w = out->width, h = out->height;
    if (w <= 0 || h <= 0) return;

    if (o->bg_tree) {
        wlr_scene_node_destroy(&o->bg_tree->node);
        o->bg_tree = NULL;
    }
    o->bg_tree = wlr_scene_tree_create(&s->scene->tree);
    if (!o->bg_tree) return;
    wlr_scene_node_lower_to_bottom(&o->bg_tree->node);

    struct wlr_box box = {0};
    wlr_output_layout_get_box(s->output_layout, out, &box);

    const int bands = 32;
    for (int i = 0; i < bands; i++) {
        int y0 = h * i / bands;
        int y1 = h * (i + 1) / bands;
        int bh = y1 - y0;
        if (bh <= 0) continue;
        float t = bands > 1 ? (float)i / (float)(bands - 1) : 0.0f;
        float col[4];
        for (int c = 0; c < 4; c++)
            col[c] = s->bg_top[c] + (s->bg_bottom[c] - s->bg_top[c]) * t;
        struct wlr_scene_rect *r =
            wlr_scene_rect_create(o->bg_tree, w, bh, col);
        if (r) wlr_scene_node_set_position(&r->node, box.x, box.y + y0);
    }
}

static void output_request_state(struct wl_listener *l, void *data)
{
    struct ycomp_output *o = wl_container_of(l, o, request_state);
    struct wlr_output_event_request_state *ev = data;
    wlr_output_commit_state(o->wlr_output, ev->state);
    output_bg_build(o);
}

static void output_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_output *o = wl_container_of(l, o, destroy);
    wl_list_remove(&o->frame.link);
    wl_list_remove(&o->request_state.link);
    wl_list_remove(&o->destroy.link);
    wl_list_remove(&o->link);
    if (o->bg_tree)
        wlr_scene_node_destroy(&o->bg_tree->node);
    free(o);
}

void ycomp_output_new(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, s->allocator, s->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct ycomp_output *o = calloc(1, sizeof(*o));
    if (!o) return;
    o->server = s;
    o->wlr_output = wlr_output;
    o->scene_output = wlr_scene_output_create(s->scene, wlr_output);

    struct wlr_output_layout_output *lo =
        wlr_output_layout_add_auto(s->output_layout, wlr_output);
    wlr_scene_output_layout_add_output(s->scene_layout, lo, o->scene_output);

    o->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &o->frame);
    o->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &o->request_state);
    o->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &o->destroy);

    wl_list_insert(&s->outputs, &o->link);

    output_bg_build(o);

    /* Разбудить первый frame: без этого на DRM нет page-flip события,
     * и output_frame никогда не будет вызван. */
    wlr_output_schedule_frame(wlr_output);
}
