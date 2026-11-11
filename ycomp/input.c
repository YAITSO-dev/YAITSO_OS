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

static void spawn_cmd(const char *cmd)
{
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

    if (!s->kb_group) return;
    if (!s->kb_group->keyboard.xkb_state) return;

    uint32_t keycode = ev->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(s->kb_group->keyboard.xkb_state,
                                       keycode, &syms);

    int handled = 0;
    if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
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
            if (sym == XKB_KEY_Tab && (mods_of(s) & 2)) {
                struct ycomp_view *v, *first = NULL;
                int found = 0, got = 0;
                wl_list_for_each(v, &s->views, link) {
                    if (!v->mapped) continue;
                    if (!first) first = v;
                    if (found) { ycomp_focus_view(s, v->id); got = 1; break; }
                    if (v->id == s->focused_id) found = 1;
                }
                if (!got && first) ycomp_focus_view(s, first->id);
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
    if (!s->kb_group) return;
    wlr_seat_set_keyboard(s->seat, &s->kb_group->keyboard);
    wlr_seat_keyboard_notify_modifiers(s->seat,
        &s->kb_group->keyboard.modifiers);
}

static void pointer_attach(struct ycomp_server *s, struct wlr_input_device *dev)
{
    wlr_cursor_attach_input_device(s->cursor, dev);
}

void ycomp_input_new(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, new_input);
    struct wlr_input_device *dev = data;

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

void ycomp_pointer_motion_delta(struct ycomp_server *s, uint32_t time_msec,
                               double dx, double dy)
{
    if (s->grabbing && s->grab_view) {
        if (s->grab_resize) {
            int nw = s->grab_view_w + (int)dx;
            int nh = s->grab_view_h + (int)dy;
            if (nw < 80) nw = 80;
            if (nh < 60) nh = 60;
            s->grab_view->w = nw;
            s->grab_view->h = nh;
            wlr_xdg_toplevel_set_size(s->grab_view->xdg_toplevel, nw, nh);
        } else {
            s->grab_view->x += (int)dx;
            s->grab_view->y += (int)dy;
            wlr_scene_node_set_position(&s->grab_view->scene_tree->node,
                                        s->grab_view->x, s->grab_view->y);
        }
        wlr_cursor_move(s->cursor, NULL, dx, dy);
        return;
    }

    wlr_cursor_move(s->cursor, NULL, dx, dy);

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);
    if (v) {
        wlr_seat_pointer_notify_enter(s->seat, surf, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(s->seat);
    }
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_pointer_motion_abs(struct ycomp_server *s, uint32_t time_msec,
                              double x, double y)
{
    wlr_cursor_warp_absolute(s->cursor, NULL, x, y);

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);
    if (v) {
        wlr_seat_pointer_notify_enter(s->seat, surf, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(s->seat);
    }
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_pointer_button_event(struct ycomp_server *s, uint32_t time_msec,
                                uint32_t button, uint32_t state)
{
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && s->grabbing) {
        s->grabbing = 0;
        s->grab_resize = 0;
        s->grab_view = NULL;
        return;
    }

    double sx, sy;
    struct wlr_surface *surf = NULL;
    struct ycomp_view *v = ycomp_view_at(s, s->cursor->x, s->cursor->y,
                                         &surf, &sx, &sy);

    if (state == WL_POINTER_BUTTON_STATE_PRESSED && v) {
        ycomp_focus_view(s, v->id);

        int mods = 0;
        if (s->kb_group && s->kb_group->keyboard.xkb_state) {
            if (xkb_state_mod_name_is_active(s->kb_group->keyboard.xkb_state,
                XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) mods = 1;
        }
        if (mods) {
            s->grab_view = v;
            s->grabbing = 1;
            s->grab_resize = (button == BTN_RIGHT);
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
    wlr_seat_pointer_notify_axis(s->seat, time_msec, orientation, delta,
                                 delta_discrete, source,
                                 WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_cursor_motion(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_motion);
    struct wlr_pointer_motion_event *ev = data;
    ycomp_pointer_motion_delta(s, ev->time_msec, ev->delta_x, ev->delta_y);
}

void ycomp_cursor_motion_abs(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *ev = data;
    ycomp_pointer_motion_abs(s, ev->time_msec, ev->x, ev->y);
}

void ycomp_cursor_button(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_button);
    struct wlr_pointer_button_event *ev = data;
    ycomp_pointer_button_event(s, ev->time_msec, ev->button, ev->state);
}

void ycomp_cursor_axis(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, cursor_axis);
    struct wlr_pointer_axis_event *ev = data;
    ycomp_pointer_axis_event(s, ev->time_msec, ev->orientation, ev->delta,
                             ev->delta_discrete, ev->source);
}

void ycomp_cursor_frame(struct wl_listener *l, void *data)
{
    (void)data;
    struct ycomp_server *s = wl_container_of(l, s, cursor_frame);
    wlr_seat_pointer_notify_frame(s->seat);
}

void ycomp_request_cursor(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    struct wlr_seat_client *focused = s->seat->pointer_state.focused_client;
    if (focused != NULL && ev->seat_client == focused)
        wlr_cursor_set_surface(s->cursor, ev->surface,
                               ev->hotspot_x, ev->hotspot_y);
}

void ycomp_request_set_selection(struct wl_listener *l, void *data)
{
    struct ycomp_server *s = wl_container_of(l, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(s->seat, ev->source, ev->serial);
}
