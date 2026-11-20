#ifndef YCOMP_H
#define YCOMP_H

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <xkbcommon/xkbcommon.h>

#define YCOMP_SOCKET    "/run/ycomp.sock"
#define YCOMP_TITLEBAR  28
#define YCOMP_BAR_DEF   28

struct ycomp_server;
struct ycomp_view;
struct ycomp_output;

struct ycomp_view {
    struct wl_list              link;
    struct ycomp_server        *server;
    struct wlr_xdg_toplevel    *xdg_toplevel;
    struct wlr_scene_tree      *container;   /* заголовок + поверхность */
    struct wlr_scene_tree      *scene_tree;  /* поверхность xdg */
    struct wlr_scene_rect      *titlebar;
    struct wlr_scene_rect      *btn_close;
    struct wlr_scene_rect      *btn_max;
    struct wlr_scene_rect      *btn_min;
    int                         id;
    int                         x, y, w, h;
    int                         mapped;
    int                         focused;
    int                         hidden;      /* свёрнуто (scene node выключен) */
    int                         maximized;
    int                         titlebar_h;
    int                         saved_x, saved_y, saved_w, saved_h;
    int                         saved_valid;

    struct wl_listener          map;
    struct wl_listener          unmap;
    struct wl_listener          destroy;
    struct wl_listener          first_commit;
    struct wl_listener          request_move;
    struct wl_listener          request_resize;
    struct wl_listener          request_maximize;
    struct wl_listener          request_fullscreen;
    struct wl_listener          set_title;
    struct wl_listener          set_app_id;
};

struct ycomp_output {
    struct wl_list              link;
    struct ycomp_server        *server;
    struct wlr_output          *wlr_output;
    struct wlr_scene_output    *scene_output;
    struct wlr_scene_tree      *bg_tree;
    struct wl_listener          frame;
    struct wl_listener          request_state;
    struct wl_listener          destroy;
};

struct ycomp_server {
    struct wl_display          *display;
    struct wl_event_loop       *event_loop;

    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct wlr_scene           *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout   *output_layout;

    struct wlr_xdg_shell       *xdg_shell;
    struct wlr_layer_shell_v1  *layer_shell;
    struct wlr_xdg_decoration_manager_v1 *deco_manager;

    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_seat            *seat;

    struct wl_listener          new_output;
    struct wl_listener          new_xdg_toplevel;
    struct wl_listener          new_xdg_popup;
    struct wl_listener          new_layer_surface;
    struct wl_listener          new_input;

    struct wl_listener          cursor_motion;
    struct wl_listener          cursor_motion_absolute;
    struct wl_listener          cursor_button;
    struct wl_listener          cursor_axis;
    struct wl_listener          cursor_frame;

    struct wl_listener          request_cursor;
    struct wl_listener          request_set_selection;

    struct wl_listener          keyboard_modifiers;
    struct wl_listener          keyboard_key;

    struct wlr_keyboard_group  *kb_group;

    struct wl_list              outputs;
    struct wl_list              views;
    struct wl_list              layers;

    int                         focused_id;
    int                         previous_id;
    int                         next_id;
    int                         bar_height;

    int                         grabbing;
    int                         grab_resize;
    uint32_t                    grab_button;
    uint32_t                    grab_edges;
    double                      grab_sx, grab_sy;
    struct ycomp_view          *grab_view;
    int                         grab_view_x, grab_view_y;
    int                         grab_view_w, grab_view_h;

    /* Двойной щелчок по заголовку — maximize/restore. */
    uint32_t                    last_click_ms;
    int                         last_click_view;
    uint32_t                    last_click_button;
    double                      last_click_x, last_click_y;

    xkb_keysym_t                hk_terminal;
    xkb_keysym_t                hk_launcher;
    xkb_keysym_t                hk_close;
    xkb_keysym_t                hk_exit;
    xkb_keysym_t                hk_maximize;
    xkb_keysym_t                hk_restore;
    xkb_keysym_t                hk_snap_left;
    xkb_keysym_t                hk_snap_right;
    xkb_keysym_t                hk_minimize;
    xkb_keysym_t                hk_next;
    xkb_keysym_t                hk_prev;
    int                         hk_terminal_mods;
    int                         hk_launcher_mods;
    int                         hk_close_mods;
    int                         hk_exit_mods;
    int                         hk_maximize_mods;
    int                         hk_restore_mods;
    int                         hk_snap_left_mods;
    int                         hk_snap_right_mods;
    int                         hk_minimize_mods;
    int                         hk_next_mods;
    int                         hk_prev_mods;

    float                       bg_top[4];
    float                       bg_bottom[4];

    int                         ipc_fd;
    struct wl_event_source     *ipc_source;
};

int  ycomp_server_init(struct ycomp_server *s);

void ycomp_output_new(struct wl_listener *, void *);
void ycomp_xdg_new_toplevel(struct wl_listener *, void *);
void ycomp_xdg_new_popup(struct wl_listener *, void *);
void ycomp_focus_view(struct ycomp_server *, int id);
void ycomp_close_view(struct ycomp_server *, int id);
int  ycomp_count_views(struct ycomp_server *);
struct ycomp_view *ycomp_view_at(struct ycomp_server *, double lx, double ly,
                                 struct wlr_surface **surface,
                                 double *sx, double *sy);
struct ycomp_view *ycomp_view_at_titlebar(struct ycomp_server *, double lx,
                                          double ly, int *zone);
int  ycomp_view_is_bar(struct ycomp_view *v);
void ycomp_view_sync_geometry(struct ycomp_view *v);
void ycomp_view_set_maximized(struct ycomp_server *, int id, int on);
void ycomp_view_snap(struct ycomp_server *, int id, int side);
void ycomp_view_minimize(struct ycomp_server *, int id, int on);
void ycomp_view_toggle_maximized(struct ycomp_server *, int id);
void ycomp_focus_next(struct ycomp_server *, int dir);
void ycomp_usable_area(struct ycomp_server *, struct wlr_box *out);

void ycomp_input_new(struct wl_listener *, void *);
void ycomp_keyboard_add(struct ycomp_server *s, struct wlr_keyboard *kb);
struct xkb_keymap *ycomp_keymap_create(void);
void ycomp_evdev_init(struct ycomp_server *s);
void ycomp_pointer_motion_delta(struct ycomp_server *, uint32_t time_msec,
                                double dx, double dy);
void ycomp_pointer_motion_abs(struct ycomp_server *, uint32_t time_msec,
                              double x, double y);
void ycomp_pointer_button_event(struct ycomp_server *, uint32_t time_msec,
                                uint32_t button, uint32_t state);
void ycomp_pointer_axis_event(struct ycomp_server *, uint32_t time_msec,
                              uint32_t orientation, double delta,
                              int32_t delta_discrete, uint32_t source);
void ycomp_cursor_motion(struct wl_listener *, void *);
void ycomp_cursor_motion_abs(struct wl_listener *, void *);
void ycomp_cursor_button(struct wl_listener *, void *);
void ycomp_cursor_axis(struct wl_listener *, void *);
void ycomp_cursor_frame(struct wl_listener *, void *);
void ycomp_request_cursor(struct wl_listener *, void *);
void ycomp_request_set_selection(struct wl_listener *, void *);

void ycomp_layer_new(struct wl_listener *, void *);
void ycomp_decorations_init(struct ycomp_server *s);

void ycomp_config_load(struct ycomp_server *s);
const char *ycomp_cfg_terminal(void);
const char *ycomp_cfg_launcher(void);

int  ycomp_ipc_init(struct ycomp_server *s);
void ycomp_ipc_fini(struct ycomp_server *s);

#endif
