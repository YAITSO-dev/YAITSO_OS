#include "ycomp.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
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

static void output_request_state(struct wl_listener *l, void *data)
{
    struct ycomp_output *o = wl_container_of(l, o, request_state);
    struct wlr_output_event_request_state *ev = data;
    wlr_output_commit_state(o->wlr_output, ev->state);
}

static void output_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_output *o = wl_container_of(l, o, destroy);
    wl_list_remove(&o->frame.link);
    wl_list_remove(&o->request_state.link);
    wl_list_remove(&o->destroy.link);
    wl_list_remove(&o->link);
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
}
