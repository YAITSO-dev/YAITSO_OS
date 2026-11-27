#include "ycomp.h"
#include "keymap-default.h"
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

static uint32_t mods_of(struct ycomp_server *s)
{
    if (!s->kb_group) return 0;
    struct xkb_state *st = s->kb_group->keyboard.xkb_state;
    uint32_t m = 0;
    if (!st) return 0;
    if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_LOGO,
        XKB_STATE_MODS_EFFECTIVE) > 0) m |= 1;
    if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_ALT,
        XKB_STATE_MODS_EFFECTIVE) > 0) m |= 2;
    if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_CTRL,
        XKB_STATE_MODS_EFFECTIVE) > 0) m |= 4;
    if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_SHIFT,
        XKB_STATE_MODS_EFFECTIVE) > 0) m |= 8;
    return m;
}

static int safe_exec_name(const char *s)
{
    if (!s || !*s) return 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '/' || c == '.' || c == '_' ||
            c == '-' || c == '+' || c == ':' || c == '@') continue;
        return 0;
    }
    return 1;
}

static void spawn_cmd(const char *cmd)
{
    if (!safe_exec_name(cmd)) {
        fprintf(stderr, "ycomp: unsafe exec name rejected: %s\n",
                cmd ? cmd : "(null)");
        return;
    }
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    execlp(cmd, cmd, (char *)NULL);
    _exit(127);
}

static void keyboard_key(struct wl_listener *l, void *data);
static void keyboard_modifiers(struct wl_listener *l, void *data);

/* В базовом rootfs YAITSO нет /usr/share/X11/xkb, поэтому
 * xkb_keymap_new_from_names() возвращает NULL. В этом случае компилируем
 * встроенную раскладку US, чтобы клавиатура работала всегда. */
struct xkb_keymap *ycomp_keymap_create(void)
{
    /* Без NO_DEFAULT_INCLUDES xkbcommon возвращает NULL из
     * xkb_context_new(), если /usr/share/X11/xkb не существует. */
    struct xkb_context *ctx =
        xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
    if (!ctx) return NULL;

    const char *root = getenv("XKB_CONFIG_ROOT");
    if (!root || !*root) root = "/usr/share/X11/xkb";
    char rules[512];
    snprintf(rules, sizeof(rules), "%s/rules/evdev", root);

    struct xkb_keymap *km = NULL;
    if (access(rules, R_OK) == 0) {
        (void)xkb_context_include_path_append(ctx, root);
        struct xkb_rule_names names = {0};
        km = xkb_keymap_new_from_names(ctx, &names,
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!km) {
        km = xkb_keymap_new_from_string(ctx, ycomp_default_keymap,
                                        XKB_KEYMAP_FORMAT_TEXT_V1,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (km)
            fprintf(stderr, "ycomp: no xkb data, using built-in US keymap\n");
    }
    xkb_context_unref(ctx);
    return km;
}

void ycomp_keyboard_add(struct ycomp_server *s, struct wlr_keyboard *kb)
{
    if (!s->kb_group) {
        s->kb_group = wlr_keyboard_group_create();
        if (!s->kb_group) {
            fprintf(stderr, "ycomp: cannot create keyboard group\n");
            return;
        }

        struct xkb_keymap *km = ycomp_keymap_create();
        if (km) {
            wlr_keyboard_set_keymap(&s->kb_group->keyboard, km);
            xkb_keymap_unref(km);
        } else {
            fprintf(stderr, "ycomp: cannot create xkb keymap\n");
        }
        wlr_keyboard_set_repeat_info(&s->kb_group->keyboard, 25, 400);

        s->keyboard_key.notify = keyboard_key;
        wl_signal_add(&s->kb_group->keyboard.events.key, &s->keyboard_key);
        s->keyboard_modifiers.notify = keyboard_modifiers;
        wl_signal_add(&s->kb_group->keyboard.events.modifiers,
                      &s->keyboard_modifiers);
    }

    /* Клавиатура группы и устройство должны иметь одинаковую раскладку,
     * иначе wlr_keyboard_group_add_keyboard() отвергает устройство. */
    if (!kb->keymap && s->kb_group->keyboard.keymap)
        wlr_keyboard_set_keymap(kb, s->kb_group->keyboard.keymap);
    wlr_keyboard_set_repeat_info(kb, 25, 400);

    if (!wlr_keyboard_group_add_keyboard(s->kb_group, kb))
        fprintf(stderr, "ycomp: cannot add keyboard to group\n");

    wlr_seat_set_keyboard(s->seat, &s->kb_group->keyboard);

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (s->kb_group) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s->seat, caps);
}

static int match_hotkey(struct ycomp_server *s, xkb_keysym_t sym,
                        xkb_keysym_t want, int want_mods)
{
    if (sym != want) return 0;
    return (int)mods_of(s) == want_mods;
}

static void keyboard_key(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, keyboard_key);
    struct wlr_keyboard_key_event *ev = data;

    if (!s || !s->kb_group) return;
    if (!s->kb_group->keyboard.xkb_state) return;

    uint32_t keycode = ev->keycode + 8;
    const xkb_keysym_t *syms = NULL;
    int nsyms = xkb_state_key_get_syms(s->kb_group->keyboard.xkb_state,
                                       keycode, &syms);

    int handled = 0;
    if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED && syms) {
        for (int i = 0; i < nsyms; i++) {
            xkb_keysym_t sym = syms[i];

            if (match_hotkey(s, sym, s->hk_terminal, s->hk_terminal_mods)) {
                spawn_cmd(ycomp_cfg_terminal());
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_launcher, s->hk_launcher_mods)) {
                spawn_cmd(ycomp_cfg_launcher());
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_close, s->hk_close_mods)) {
                if (s->focused_id > 0) ycomp_close_view(s, s->focused_id);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_exit, s->hk_exit_mods)) {
                wl_display_terminate(s->display);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_maximize, s->hk_maximize_mods)) {
                if (s->focused_id > 0)
                    ycomp_view_toggle_maximized(s, s->focused_id);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_restore, s->hk_restore_mods)) {
                if (s->focused_id > 0)
                    ycomp_view_set_maximized(s, s->focused_id, 0);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_snap_left, s->hk_snap_left_mods)) {
                if (s->focused_id > 0)
                    ycomp_view_snap(s, s->focused_id, 0);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_snap_right, s->hk_snap_right_mods)) {
                if (s->focused_id > 0)
                    ycomp_view_snap(s, s->focused_id, 1);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_minimize, s->hk_minimize_mods)) {
                if (s->focused_id > 0)
                    ycomp_view_minimize(s, s->focused_id, 1);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_next, s->hk_next_mods)) {
                ycomp_focus_next(s, 1);
                handled = 1;
                break;
            }
            if (match_hotkey(s, sym, s->hk_prev, s->hk_prev_mods)) {
                ycomp_focus_next(s, -1);
                handled = 1;
                break;
            }
            if (sym == XKB_KEY_Tab && (mods_of(s) & 2)) {
                ycomp_focus_next(s, (mods_of(s) & 8) ? -1 : 1);
                handled = 1;
                break;
            }
            if (sym == XKB_KEY_Tab && (mods_of(s) & 1)) {
                ycomp_focus_next(s, (mods_of(s) & 8) ? -1 : 1);
                handled = 1;
                break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(s->seat, &s->kb_group->keyboard);
        wlr_seat_keyboard_notify_key(s->seat, ev->time_msec,
                                     ev->keycode, ev->state);
    }
}

static void keyboard_modifiers(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_server *s = wl_container_of(l, s, keyboard_modifiers);
    if (!s || !s->kb_group) return;
    wlr_seat_set_keyboard(s->seat, &s->kb_group->keyboard);
    wlr_seat_keyboard_notify_modifiers(s->seat,
        &s->kb_group->keyboard.modifiers);
}

static void pointer_attach(struct ycomp_server *s, struct wlr_input_device *dev)
{
    if (!s || !s->cursor || !dev) return;
    wlr_cursor_attach_input_device(s->cursor, dev);
}

void ycomp_input_new(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_input);
    struct wlr_input_device *dev = data;
    if (!s || !dev) return;

    switch (dev->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard *kb = wlr_keyboard_from_input_device(dev);
        ycomp_keyboard_add(s, kb);
        break;
    }
    case WLR_INPUT_DEVICE_POINTER:
        pointer_attach(s, dev);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (s->kb_group) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s->seat, caps);
}

/* Применяет перетаскивание/изменение размера от исходной геометрии.
 * Так окно не «уезжает» при смене направления движения. */
static void grab_apply(struct ycomp_server *s, double total_dx, double total_dy)
{
    struct ycomp_view *v = s->grab_view;
    if (!v) return;

    if (s->grab_resize) {
        int dx = (int)total_dx;
        int dy = (int)total_dy;
        int nx = s->grab_view_x, ny = s->grab_view_y;
        int nw = s->grab_view_w, nh = s->grab_view_h;

        if (s->grab_edges & WLR_EDGE_LEFT) {
            nx = s->grab_view_x + dx;
            nw = s->grab_view_w - dx;
        } else if (s->grab_edges & WLR_EDGE_RIGHT) {
            nw = s->grab_view_w + dx;
        }
        if (s->grab_edges & WLR_EDGE_TOP) {
            ny = s->grab_view_y + dy;
            nh = s->grab_view_h - dy;
        } else if (s->grab_edges & WLR_EDGE_BOTTOM) {
            nh = s->grab_view_h + dy;
        }

        if (nw < 160) {
            if (s->grab_edges & WLR_EDGE_LEFT)
                nx = s->grab_view_x + s->grab_view_w - 160;
            nw = 160;
        }
        if (nh < 80) {
            if (s->grab_edges & WLR_EDGE_TOP)
                ny = s->grab_view_y + s->grab_view_h - 80;
            nh = 80;
        }

        v->x = nx;
        v->y = ny;
        v->w = nw;
        v->h = nh;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, nw, nh);
    } else {
        v->x = s->grab_view_x + (int)total_dx;
        v->y = s->grab_view_y + (int)total_dy;
        /* Заголовок всегда остаётся в пределах экрана по вертикали. */
        struct wlr_box u;
        ycomp_usable_area(s, &u);
        if (v->y < u.y) v->y = u.y;
        if (v->y > u.y + u.height - 30) v->y = u.y + u.height - 30;
    }
    ycomp_view_sync_geometry(v);
}

void ycomp_pointer_motion_delta(struct ycomp_server *s, uint32_t time_msec,
                               double dx, double dy)
{
    if (!s || !s->cursor) return;
    if (s->grabbing && s->grab_view) {
        wlr_cursor_move(s->cursor, NULL, dx, dy);
        grab_apply(s, s->cursor->x - s->grab_sx,
                   s->cursor->y - s->grab_sy);
        return;
    }

    wlr_cursor_move(s->cursor, NULL, dx, dy);

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);
    if (v) {
        if (wlr_seat_pointer_surface_has_focus(s->seat, surf))
            wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
        else
            wlr_seat_pointer_notify_enter(s->seat, surf, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(s->seat);
    }
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_pointer_motion_abs(struct ycomp_server *s, uint32_t time_msec,
                              double x, double y)
{
    if (!s || !s->cursor) return;
    wlr_cursor_warp_absolute(s->cursor, NULL, x, y);

    if (s->grabbing && s->grab_view) {
        grab_apply(s, s->cursor->x - s->grab_sx,
                   s->cursor->y - s->grab_sy);
        return;
    }

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);
    if (v) {
        if (wlr_seat_pointer_surface_has_focus(s->seat, surf))
            wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
        else
            wlr_seat_pointer_notify_enter(s->seat, surf, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(s->seat);
    }
    wlr_seat_pointer_notify_frame(s->seat);
}

/* Окно под курсором, если курсор у его края или угла (6 px). Позволяет
 * менять размер обычным перетаскиванием рамки, без модификаторов. */
static struct ycomp_view *view_edges_at(struct ycomp_server *s, double lx,
                                        double ly, uint32_t *edges)
{
    struct ycomp_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped || v->hidden || ycomp_view_is_bar(v)) continue;
        if (v->titlebar_h <= 0 || v->maximized) continue;
        int total_h = v->titlebar_h + v->h;
        if (lx < v->x || lx >= v->x + v->w) continue;
        if (ly < v->y || ly >= v->y + total_h) continue;
        uint32_t e = 0;
        if (lx < v->x + 6)                          e |= WLR_EDGE_LEFT;
        else if (lx >= v->x + v->w - 6)             e |= WLR_EDGE_RIGHT;
        if (ly < v->y + 6)                          e |= WLR_EDGE_TOP;
        else if (ly >= v->y + total_h - 6)          e |= WLR_EDGE_BOTTOM;
        if (e) { *edges = e; return v; }
    }
    return NULL;
}

void ycomp_pointer_button_event(struct ycomp_server *s, uint32_t time_msec,
                                uint32_t button, uint32_t state)
{
    if (!s || !s->cursor) return;
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->grabbing) {
        s->grabbing = 0;
        s->grab_resize = 0;
        s->grab_view = NULL;
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        uint32_t edges = 0;
        struct ycomp_view *ev =
            view_edges_at(s, s->cursor->x, s->cursor->y, &edges);
        if (ev && button == BTN_LEFT) {
            ycomp_focus_view(s, ev->id);
            s->grab_view = ev;
            s->grabbing = 1;
            s->grab_resize = 1;
            s->grab_edges = edges;
            s->grab_sx = s->cursor->x;
            s->grab_sy = s->cursor->y;
            s->grab_view_x = ev->x;
            s->grab_view_y = ev->y;
            s->grab_view_w = ev->w;
            s->grab_view_h = ev->h;
            return;
        }

        int zone = 0;
        struct ycomp_view *tv = ycomp_view_at_titlebar(s, s->cursor->x,
                                                       s->cursor->y, &zone);
        if (tv) {
            if (zone == 1) {
                ycomp_close_view(s, tv->id);
            } else if (zone == 2) {
                ycomp_view_toggle_maximized(s, tv->id);
            } else if (zone == 3) {
                ycomp_view_minimize(s, tv->id, 1);
            } else {
                ycomp_focus_view(s, tv->id);

                int dbl = (s->last_click_view == tv->id &&
                           s->last_click_button == button &&
                           time_msec - s->last_click_ms < 400 &&
                           s->cursor->x - s->last_click_x < 6 &&
                           s->cursor->x - s->last_click_x > -6);
                if (dbl) {
                    ycomp_view_toggle_maximized(s, tv->id);
                } else {
                    s->grab_view = tv;
                    s->grabbing = 1;
                    s->grab_resize = 0;
                    s->grab_sx = s->cursor->x;
                    s->grab_sy = s->cursor->y;
                    s->grab_view_x = tv->x;
                    s->grab_view_y = tv->y;
                    s->grab_view_w = tv->w;
                    s->grab_view_h = tv->h;
                }
            }
            s->last_click_view = tv->id;
            s->last_click_button = button;
            s->last_click_ms = time_msec;
            s->last_click_x = s->cursor->x;
            s->last_click_y = s->cursor->y;
            return;
        }
    }

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);

    if (state == WL_POINTER_BUTTON_STATE_PRESSED && v) {
        /* Панель не должна забирать клавиатурный фокус. */
        if (!ycomp_view_is_bar(v))
            ycomp_focus_view(s, v->id);

        int mods = 0;
        if (s->kb_group && s->kb_group->keyboard.xkb_state) {
            if (xkb_state_mod_name_is_active(s->kb_group->keyboard.xkb_state,
                XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) mods = 1;
        }
        if (mods && !ycomp_view_is_bar(v)) {
            s->grab_view = v;
            s->grabbing = 1;
            s->grab_resize = (button == BTN_RIGHT);
            s->grab_edges = (button == BTN_RIGHT) ? WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT
                                                  : 0;
            s->grab_sx = s->cursor->x;
            s->grab_sy = s->cursor->y;
            s->grab_view_x = v->x;
            s->grab_view_y = v->y;
            s->grab_view_w = v->w;
            s->grab_view_h = v->h;
            return;
        }
    }

    if (v && surf) {
        wlr_seat_pointer_notify_button(s->seat, time_msec, button, state);
    }
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_pointer_axis_event(struct ycomp_server *s, uint32_t time_msec,
                              uint32_t orientation, double delta,
                              int32_t delta_discrete, uint32_t source)
{
    if (!s) return;
    wlr_seat_pointer_notify_axis(s->seat, time_msec, orientation, delta,
                                 delta_discrete, source,
                                 WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_cursor_motion(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_motion);
    struct wlr_pointer_motion_event *ev = data;
    if (!s || !ev) return;
    ycomp_pointer_motion_delta(s, ev->time_msec, ev->delta_x, ev->delta_y);
}

void ycomp_cursor_motion_abs(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *ev = data;
    if (!s || !ev) return;
    ycomp_pointer_motion_abs(s, ev->time_msec, ev->x, ev->y);
}

void ycomp_cursor_button(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_button);
    struct wlr_pointer_button_event *ev = data;
    if (!s || !ev) return;
    ycomp_pointer_button_event(s, ev->time_msec, ev->button, ev->state);
}

void ycomp_cursor_axis(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_axis);
    struct wlr_pointer_axis_event *ev = data;
    if (!s || !ev) return;
    ycomp_pointer_axis_event(s, ev->time_msec, ev->orientation, ev->delta,
                             ev->delta_discrete, ev->source);
}

void ycomp_cursor_frame(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_server *s = wl_container_of(l, s, cursor_frame);
    if (!s) return;
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_request_cursor(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (!s || !ev) return;
    struct wlr_seat_client *focused = s->seat->pointer_state.focused_client;
    if (focused != NULL && ev->seat_client == focused)
        wlr_cursor_set_surface(s->cursor, ev->surface,
                               ev->hotspot_x, ev->hotspot_y);
}

void ycomp_request_set_selection(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *ev = data;
    if (!s || !ev) return;
    wlr_seat_set_selection(s->seat, ev->source, ev->serial);
}
