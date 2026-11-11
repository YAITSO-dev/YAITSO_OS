#ifndef YWT_INTERNAL_H
#define YWT_INTERNAL_H

#include "libywt.h"
#include "xdg-shell-client-protocol.h"

struct ywt_ctx {
    struct wl_display    *display;
    struct wl_registry   *registry;
    struct wl_compositor *compositor;
    struct wl_shm        *shm;
    struct xdg_wm_base   *wm_base;
    struct wl_seat       *seat;
    struct wl_output     *output;
    int                   output_w, output_h;
    int                   output_version;
    int                   seat_caps;
    struct wl_keyboard   *keyboard;
    struct wl_pointer    *pointer;
    void                 *input_state;
};

struct ywt_surface {
    ywt_ctx_t           *ctx;
    struct wl_surface   *wl;
    struct xdg_surface  *xdg;
    struct xdg_toplevel *top;
    int                  w, h;
    int                  closed;
    int                  configured;
    int                  pending_w, pending_h;

    void                *cb_data;
    ywt_key_cb           on_key;
    ywt_pointer_cb       on_ptr;
    ywt_motion_cb        on_motion;
    ywt_axis_cb          on_axis;
    ywt_close_cb         on_close;
    ywt_configure_cb     on_configure;
    ywt_frame_cb         on_frame;
};

struct wl_compositor *ywt_ctx_compositor(ywt_ctx_t *);
struct wl_shm        *ywt_ctx_shm(ywt_ctx_t *);
struct xdg_wm_base   *ywt_ctx_wm_base(ywt_ctx_t *);
struct wl_seat       *ywt_ctx_seat(ywt_ctx_t *);

void ywt_input_attach(ywt_ctx_t *);
void ywt_input_bind_seat(ywt_ctx_t *);
void ywt_input_detach(ywt_ctx_t *);
void ywt_input_set_active(ywt_surface_t *);

#endif
