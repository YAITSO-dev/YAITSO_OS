#include "ywt-internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

struct ywt_input {
    ywt_ctx_t          *ctx;
    ywt_surface_t      *active;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap  *xkb_keymap;
    struct xkb_state   *xkb_state;
    int                 px, py;
    int                 ptr_inside;
};

static struct ywt_input g_in = {0};

static ywt_surface_t *input_surface(struct ywt_input *in)
{
    return in->active;
}

static void kb_keymap(void *d, struct wl_keyboard *kb, uint32_t fmt,
                      int32_t fd, uint32_t size)
{
    (void)kb;
    struct ywt_input *in = d;
    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    if (!in->xkb_ctx) in->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (in->xkb_keymap) xkb_keymap_unref(in->xkb_keymap);
    if (in->xkb_state)  xkb_state_unref(in->xkb_state);
    in->xkb_keymap = xkb_keymap_new_from_string(in->xkb_ctx, map,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (in->xkb_keymap) in->xkb_state = xkb_state_new(in->xkb_keymap);
}

static void kb_enter(void *d, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *ws, struct wl_array *keys)
{
    (void)kb; (void)serial; (void)keys; (void)ws;
    struct ywt_input *in = d;
    (void)in;
}

static void kb_leave(void *d, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *ws)
{
    (void)d; (void)kb; (void)serial; (void)ws;
}

static uint32_t compute_mods(struct ywt_input *in)
{
    uint32_t mods = 0;
    if (!in->xkb_state) return 0;
    if (xkb_state_mod_name_is_active(in->xkb_state, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= YWT_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(in->xkb_state, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= YWT_MOD_CTRL;
    if (xkb_state_mod_name_is_active(in->xkb_state, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= YWT_MOD_ALT;
    if (xkb_state_mod_name_is_active(in->xkb_state, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= YWT_MOD_SUPER;
    return mods;
}

static void kb_key(void *d, struct wl_keyboard *kb, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state)
{
    (void)kb; (void)serial; (void)time;
    struct ywt_input *in = d;
    ywt_surface_t *s = input_surface(in);
    if (!s || !s->on_key || !in->xkb_state) return;

    xkb_keycode_t kc = (xkb_keycode_t)(key + 8);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(in->xkb_state, kc);
    uint32_t mods = compute_mods(in);

    char utf8[32] = {0};
    xkb_state_key_get_utf8(in->xkb_state, kc, utf8, sizeof(utf8));

    s->on_key(s->cb_data, sym, key, state, mods, utf8);
}

static void kb_modifiers(void *d, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t dep, uint32_t lat, uint32_t lock,
                         uint32_t group)
{
    (void)kb; (void)serial;
    struct ywt_input *in = d;
    if (!in->xkb_state) return;
    xkb_state_update_mask(in->xkb_state, dep, lat, lock, 0, 0, group);
}

static void kb_repeat(void *d, struct wl_keyboard *kb,
                      int32_t rate, int32_t delay)
{
    (void)d; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_modifiers,
    .repeat_info = kb_repeat,
};

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *ws, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)p; (void)serial; (void)ws;
    struct ywt_input *in = d;
    in->px = wl_fixed_to_int(sx);
    in->py = wl_fixed_to_int(sy);
    in->ptr_inside = 1;
    ywt_surface_t *s = input_surface(in);
    if (s && s->on_motion) s->on_motion(s->cb_data, in->px, in->py);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *ws)
{
    (void)p; (void)serial; (void)ws;
    struct ywt_input *in = d;
    in->ptr_inside = 0;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time,
                       wl_fixed_t sx, wl_fixed_t sy)
{
    (void)p; (void)time;
    struct ywt_input *in = d;
    in->px = wl_fixed_to_int(sx);
    in->py = wl_fixed_to_int(sy);
    ywt_surface_t *s = input_surface(in);
    if (s && s->on_motion) s->on_motion(s->cb_data, in->px, in->py);
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state)
{
    (void)p; (void)serial; (void)time;
    struct ywt_input *in = d;
    ywt_surface_t *s = input_surface(in);
    if (s && s->on_ptr) s->on_ptr(s->cb_data, in->px, in->py, button, state);
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time,
                     uint32_t axis, wl_fixed_t value)
{
    (void)p; (void)time;
    struct ywt_input *in = d;
    ywt_surface_t *s = input_surface(in);
    if (s && s->on_axis)
        s->on_axis(s->cb_data, in->px, in->py, axis,
                   wl_fixed_to_int(value));
}

static void ptr_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }

static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
    struct ywt_input *in = d;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !in->ctx->keyboard) {
        in->ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(in->ctx->keyboard, &kb_listener, in);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !in->ctx->pointer) {
        in->ctx->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(in->ctx->pointer, &ptr_listener, in);
    }
    in->ctx->seat_caps = caps;
}

static void seat_name(void *d, struct wl_seat *s, const char *n)
{
    (void)d; (void)s; (void)n;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps,
    .name = seat_name,
};

void ywt_input_set_active(ywt_surface_t *s)
{
    g_in.active = s;
}

void ywt_input_attach(ywt_ctx_t *c)
{
    if (!c->seat) return;
    g_in.ctx = c;
    c->input_state = &g_in;
    wl_seat_add_listener(c->seat, &seat_listener, &g_in);
    wl_display_roundtrip(c->display);
}

void ywt_input_detach(ywt_ctx_t *c)
{
    if (c->pointer)  wl_pointer_destroy(c->pointer);
    if (c->keyboard) wl_keyboard_destroy(c->keyboard);
    if (g_in.xkb_state)  xkb_state_unref(g_in.xkb_state);
    if (g_in.xkb_keymap) xkb_keymap_unref(g_in.xkb_keymap);
    if (g_in.xkb_ctx)    xkb_context_unref(g_in.xkb_ctx);
    memset(&g_in, 0, sizeof(g_in));
    c->pointer = NULL;
    c->keyboard = NULL;
}
