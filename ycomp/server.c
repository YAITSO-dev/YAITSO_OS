#include "ycomp.h"
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ycomp_server_init(struct ycomp_server *s)
{
    const char *log_level = getenv("WLR_LOG");
    wlr_log_init(log_level && !strcmp(log_level, "debug") ? WLR_DEBUG : WLR_INFO,
                 NULL);

    s->display = wl_display_create();
    if (!s->display) return -1;
    s->event_loop = wl_display_get_event_loop(s->display);

    s->backend = wlr_backend_autocreate(s->event_loop, NULL);
    if (!s->backend) { fprintf(stderr, "backend autocreate failed\n"); return -1; }

    s->renderer = wlr_renderer_autocreate(s->backend);
    if (!s->renderer) return -1;
    wlr_renderer_init_wl_display(s->renderer, s->display);

    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    if (!s->allocator) return -1;

    s->scene = wlr_scene_create();
    s->output_layout = wlr_output_layout_create(s->display);
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    /* Без wl_compositor клиенты не могут создать wl_surface — ybar/yterm
     * получают "no display" и рабочий стол остаётся чёрным. */
    if (!wlr_compositor_create(s->display, 6, s->renderer)) return -1;
    wlr_subcompositor_create(s->display);
    wlr_data_device_manager_create(s->display);

    s->xdg_shell = wlr_xdg_shell_create(s->display, 3);
    s->layer_shell = wlr_layer_shell_v1_create(s->display, 4);
    s->deco_manager = wlr_xdg_decoration_manager_v1_create(s->display);

    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");

    s->seat = wlr_seat_create(s->display, "seat0");

    wl_list_init(&s->outputs);
    wl_list_init(&s->views);
    wl_list_init(&s->layers);

    s->new_output.notify = ycomp_output_new;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->new_xdg_toplevel.notify = ycomp_xdg_new_toplevel;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);

    s->new_xdg_popup.notify = ycomp_xdg_new_popup;
    wl_signal_add(&s->xdg_shell->events.new_popup, &s->new_xdg_popup);

    s->new_layer_surface.notify = ycomp_layer_new;
    wl_signal_add(&s->layer_shell->events.new_surface, &s->new_layer_surface);

    s->new_input.notify = ycomp_input_new;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);

    s->cursor_motion.notify = ycomp_cursor_motion;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);
    s->cursor_motion_absolute.notify = ycomp_cursor_motion_abs;
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);
    s->cursor_button.notify = ycomp_cursor_button;
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);
    s->cursor_axis.notify = ycomp_cursor_axis;
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);
    s->cursor_frame.notify = ycomp_cursor_frame;
    wl_signal_add(&s->cursor->events.frame, &s->cursor_frame);

    s->request_cursor.notify = ycomp_request_cursor;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_cursor);
    s->request_set_selection.notify = ycomp_request_set_selection;
    wl_signal_add(&s->seat->events.request_set_selection, &s->request_set_selection);

    return 0;
}
