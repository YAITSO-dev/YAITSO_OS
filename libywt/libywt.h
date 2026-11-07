#ifndef LIBYWT_H
#define LIBYWT_H

#include <stdint.h>
#include <stddef.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct ywt_ctx;
struct ywt_surface;
struct ywt_buf;
struct ywt_font;

typedef struct ywt_ctx     ywt_ctx_t;
typedef struct ywt_surface ywt_surface_t;
typedef struct ywt_buf     ywt_buf_t;
typedef struct ywt_font    ywt_font_t;

#define YWT_MOD_SHIFT   0x01
#define YWT_MOD_CTRL    0x02
#define YWT_MOD_ALT     0x04
#define YWT_MOD_SUPER   0x08

typedef void (*ywt_key_cb)(void *data, xkb_keysym_t sym, uint32_t keycode,
                           uint32_t state, uint32_t mods, const char *utf8);
typedef void (*ywt_pointer_cb)(void *data, int x, int y, uint32_t button, uint32_t state);
typedef void (*ywt_motion_cb)(void *data, int x, int y);
typedef void (*ywt_axis_cb)(void *data, int x, int y, uint32_t axis, int32_t value);
typedef void (*ywt_close_cb)(void *data);
typedef void (*ywt_configure_cb)(void *data, int w, int h);
typedef void (*ywt_frame_cb)(void *data);

ywt_ctx_t *ywt_ctx_create(void);
void       ywt_ctx_destroy(ywt_ctx_t *);
struct wl_display    *ywt_ctx_display(ywt_ctx_t *);
int                   ywt_ctx_dispatch(ywt_ctx_t *);
int                   ywt_ctx_dispatch_pending(ywt_ctx_t *);
int                   ywt_ctx_flush(ywt_ctx_t *);
int                   ywt_ctx_roundtrip(ywt_ctx_t *);
int                   ywt_ctx_fd(ywt_ctx_t *);
struct wl_compositor *ywt_ctx_compositor(ywt_ctx_t *);
struct wl_shm        *ywt_ctx_shm(ywt_ctx_t *);
struct xdg_wm_base   *ywt_ctx_wm_base(ywt_ctx_t *);
struct wl_seat       *ywt_ctx_seat(ywt_ctx_t *);
int                   ywt_ctx_output_w(ywt_ctx_t *);
int                   ywt_ctx_output_h(ywt_ctx_t *);

ywt_surface_t *ywt_surface_create(ywt_ctx_t *, int w, int h,
                                  const char *title, const char *app_id);
void ywt_surface_destroy(ywt_surface_t *);
void ywt_surface_set_cb(ywt_surface_t *, void *data,
                        ywt_key_cb, ywt_pointer_cb, ywt_motion_cb,
                        ywt_axis_cb, ywt_close_cb, ywt_configure_cb,
                        ywt_frame_cb);
ywt_ctx_t *ywt_surface_ctx(ywt_surface_t *);
struct wl_surface *ywt_surface_wl(ywt_surface_t *);
int  ywt_surface_width(ywt_surface_t *);
int  ywt_surface_height(ywt_surface_t *);
void ywt_surface_commit(ywt_surface_t *);
int  ywt_surface_should_close(ywt_surface_t *);

struct wl_buffer *ywt_buf_wl(ywt_buf_t *);
ywt_buf_t *ywt_buf_create(ywt_surface_t *, int w, int h);
void       ywt_buf_destroy(ywt_buf_t *);
uint32_t  *ywt_buf_data(ywt_buf_t *);
int        ywt_buf_width(ywt_buf_t *);
int        ywt_buf_height(ywt_buf_t *);
int        ywt_buf_stride(ywt_buf_t *);
void       ywt_buf_clear(ywt_buf_t *, uint32_t argb);

ywt_font_t *ywt_font_load(const char *path, int size);
void        ywt_font_destroy(ywt_font_t *);
int         ywt_font_width(ywt_font_t *);
int         ywt_font_height(ywt_font_t *);
int         ywt_font_ascent(ywt_font_t *);
int         ywt_text_width(ywt_font_t *, const char *s);

void ywt_draw_rect(ywt_buf_t *, int x, int y, int w, int h, uint32_t argb);
void ywt_draw_rect_blend(ywt_buf_t *, int x, int y, int w, int h, uint32_t argb);
void ywt_draw_text(ywt_buf_t *, ywt_font_t *, const char *s,
                   int x, int y, uint32_t argb);
void ywt_draw_hline(ywt_buf_t *, int x, int y, int w, uint32_t argb);
void ywt_draw_vline(ywt_buf_t *, int x, int y, int h, uint32_t argb);
void ywt_font_glyph(ywt_font_t *, ywt_buf_t *, uint32_t cp,
                    int x, int y, uint32_t argb);

#endif
