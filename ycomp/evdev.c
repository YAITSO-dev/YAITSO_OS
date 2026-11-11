/* Прямое чтение /dev/input/event* — резервный путь ввода.
 *
 * wlroots/libinput требуют udev-базу и /usr/share/libinput, которых нет в
 * минимальном rootfs YAITSO. Поэтому, если backend не создал ни одного
 * устройства ввода (WLR_BACKENDS=drm), композитор сам читает evdev и
 * передаёт события в wlr_keyboard/wlr_cursor.
 */
#include "ycomp.h"
#include <wlr/interfaces/wlr_keyboard.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define EVDEV_MAX_PATH    32
#define EVDEV_PENDING_BTN 8

struct ycomp_evdev_button {
    uint32_t code;
    uint32_t state;
};

struct ycomp_evdev {
    struct ycomp_server        *server;
    int                         fd;
    int                         is_keyboard;
    int                         is_pointer;
    int                         is_absolute;
    int                         abs_x_min, abs_x_max;
    int                         abs_y_min, abs_y_max;
    int                         abs_x, abs_y;
    int                         have_abs_x, have_abs_y;
    int                         pending_dx, pending_dy;
    int                         pending_wheel, pending_hwheel;

    struct ycomp_evdev_button   buttons[EVDEV_PENDING_BTN];
    int                         num_buttons;

    struct wlr_keyboard         keyboard;
    struct wl_event_source     *source;
};

static uint32_t now_msec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static int bit_set(const uint8_t *bits, int nr)
{
    return bits[nr / 8] & (1u << (nr % 8));
}

static void write_input_event(int fd, const struct input_event *ev)
{
    ssize_t n = write(fd, ev, sizeof(*ev));
    (void)n;
}

static void evdev_led_update(struct wlr_keyboard *kb, uint32_t leds)
{
    struct ycomp_evdev *ed = wl_container_of(kb, ed, keyboard);
    if (ed->fd < 0) return;

    struct input_event ev = {0};
    ev.type = EV_LED;

    ev.code = LED_CAPSL;
    ev.value = (leds & WLR_LED_CAPS_LOCK) ? 1 : 0;
    write_input_event(ed->fd, &ev);

    ev.code = LED_NUML;
    ev.value = (leds & WLR_LED_NUM_LOCK) ? 1 : 0;
    write_input_event(ed->fd, &ev);

    ev.code = LED_SCROLLL;
    ev.value = (leds & WLR_LED_SCROLL_LOCK) ? 1 : 0;
    write_input_event(ed->fd, &ev);

    struct input_event syn = {0};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    write_input_event(ed->fd, &syn);
}

static const struct wlr_keyboard_impl evdev_keyboard_impl = {
    .name = "ycomp-evdev",
    .led_update = evdev_led_update,
};

static void probe_device(struct ycomp_evdev *ed, const char *path)
{
    uint8_t evbits[(EV_MAX + 8) / 8];
    memset(evbits, 0, sizeof(evbits));
    if (ioctl(ed->fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return;

    int has_key = bit_set(evbits, EV_KEY);
    int has_rel = bit_set(evbits, EV_REL);
    int has_abs = bit_set(evbits, EV_ABS);

    if (has_key) {
        uint8_t keybits[(KEY_MAX + 8) / 8];
        memset(keybits, 0, sizeof(keybits));
        if (ioctl(ed->fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
            /* Настоящая клавиатура: есть буквы/Enter, а не только кнопки
             * питания или BTN_LEFT мыши. */
            if (bit_set(keybits, KEY_A) && bit_set(keybits, KEY_ENTER))
                ed->is_keyboard = 1;

            if (bit_set(keybits, BTN_LEFT) || bit_set(keybits, BTN_TOUCH))
                ed->is_pointer = 1;
            if (bit_set(keybits, BTN_TOOL_FINGER))
                ed->is_pointer = 1;
        }
    }

    if (has_rel) {
        uint8_t relbits[(REL_MAX + 8) / 8];
        memset(relbits, 0, sizeof(relbits));
        if (ioctl(ed->fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits) >= 0) {
            if (bit_set(relbits, REL_X) || bit_set(relbits, REL_Y))
                ed->is_pointer = 1;
        }
    }

    if (has_abs) {
        uint8_t absbits[(ABS_MAX + 8) / 8];
        memset(absbits, 0, sizeof(absbits));
        if (ioctl(ed->fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
            if (bit_set(absbits, ABS_X) && bit_set(absbits, ABS_Y)) {
                ed->is_pointer = 1;
                ed->is_absolute = 1;

                struct input_absinfo info;
                if (ioctl(ed->fd, EVIOCGABS(ABS_X), &info) == 0) {
                    ed->abs_x_min = info.minimum;
                    ed->abs_x_max = info.maximum;
                }
                if (ed->abs_x_max <= ed->abs_x_min)
                    ed->is_absolute = 0;
                if (ioctl(ed->fd, EVIOCGABS(ABS_Y), &info) == 0) {
                    ed->abs_y_min = info.minimum;
                    ed->abs_y_max = info.maximum;
                }
                if (ed->abs_y_max <= ed->abs_y_min)
                    ed->is_absolute = 0;
            }
        }
    }

    (void)path;
}

static void flush_buttons(struct ycomp_evdev *ed)
{
    for (int i = 0; i < ed->num_buttons; i++) {
        ycomp_pointer_button_event(ed->server, now_msec(),
                                   ed->buttons[i].code,
                                   ed->buttons[i].state);
    }
    ed->num_buttons = 0;
}

static void flush_frame(struct ycomp_evdev *ed)
{
    if (!ed->is_pointer) return;

    if (ed->is_absolute && (ed->have_abs_x || ed->have_abs_y)) {
        if (!ed->have_abs_x) ed->abs_x = (ed->abs_x_min + ed->abs_x_max) / 2;
        if (!ed->have_abs_y) ed->abs_y = (ed->abs_y_min + ed->abs_y_max) / 2;

        double x = (double)(ed->abs_x - ed->abs_x_min) /
                   (double)(ed->abs_x_max - ed->abs_x_min);
        double y = (double)(ed->abs_y - ed->abs_y_min) /
                   (double)(ed->abs_y_max - ed->abs_y_min);
        if (x < 0) x = 0;
        if (x > 1) x = 1;
        if (y < 0) y = 0;
        if (y > 1) y = 1;

        ycomp_pointer_motion_abs(ed->server, now_msec(), x, y);
        ed->have_abs_x = ed->have_abs_y = 0;
    } else if (ed->pending_dx || ed->pending_dy) {
        ycomp_pointer_motion_delta(ed->server, now_msec(),
                                   ed->pending_dx, ed->pending_dy);
        ed->pending_dx = ed->pending_dy = 0;
    }

    if (ed->num_buttons) flush_buttons(ed);

    if (ed->pending_wheel) {
        ycomp_pointer_axis_event(ed->server, now_msec(),
                                 WL_POINTER_AXIS_VERTICAL_SCROLL,
                                 -ed->pending_wheel * 15.0,
                                 -ed->pending_wheel,
                                 WL_POINTER_AXIS_SOURCE_WHEEL);
        ed->pending_wheel = 0;
    }
    if (ed->pending_hwheel) {
        ycomp_pointer_axis_event(ed->server, now_msec(),
                                 WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                 -ed->pending_hwheel * 15.0,
                                 -ed->pending_hwheel,
                                 WL_POINTER_AXIS_SOURCE_WHEEL);
        ed->pending_hwheel = 0;
    }
}

static void handle_event(struct ycomp_evdev *ed, struct input_event *ev)
{
    switch (ev->type) {
    case EV_KEY:
        if (ed->is_keyboard && ev->code < BTN_MISC) {
            /* Автоповтор ядра не пересылаем: клиенты (GTK/Qt/терминал)
             * делают его сами по wlr_keyboard repeat_info. */
            if (ev->value != 0 && ev->value != 1) break;

            struct wlr_keyboard_key_event kev = {
                .time_msec = now_msec(),
                .keycode = ev->code,
                .state = ev->value ? WL_KEYBOARD_KEY_STATE_PRESSED
                                   : WL_KEYBOARD_KEY_STATE_RELEASED,
                .update_state = true,
            };
            wlr_keyboard_notify_key(&ed->keyboard, &kev);
        } else if (ed->is_pointer && ev->code >= BTN_MISC) {
            if (ev->value != 0 && ev->value != 1) break;
            if (ed->num_buttons < EVDEV_PENDING_BTN) {
                ed->buttons[ed->num_buttons].code = ev->code;
                ed->buttons[ed->num_buttons].state = ev->value
                    ? WL_POINTER_BUTTON_STATE_PRESSED
                    : WL_POINTER_BUTTON_STATE_RELEASED;
                ed->num_buttons++;
            }
        }
        break;

    case EV_REL:
        if (!ed->is_pointer || ed->is_absolute) break;
        switch (ev->code) {
        case REL_X: ed->pending_dx += ev->value; break;
        case REL_Y: ed->pending_dy += ev->value; break;
        case REL_WHEEL: ed->pending_wheel += ev->value; break;
        case REL_HWHEEL: ed->pending_hwheel += ev->value; break;
        }
        break;

    case EV_ABS:
        if (!ed->is_pointer || !ed->is_absolute) break;
        switch (ev->code) {
        case ABS_X:
            ed->abs_x = ev->value;
            ed->have_abs_x = 1;
            break;
        case ABS_Y:
            ed->abs_y = ev->value;
            ed->have_abs_y = 1;
            break;
        }
        break;

    case EV_SYN:
        if (ev->code == SYN_REPORT) flush_frame(ed);
        break;

    default:
        break;
    }
}

static int evdev_dispatch(int fd, uint32_t mask, void *data)
{
    struct ycomp_evdev *ed = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ed->source = NULL;
        return 0;
    }

    struct input_event events[32];
    for (;;) {
        ssize_t n = read(fd, events, sizeof(events));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) break;
            break;
        }
        if (n == 0) break;

        size_t count = (size_t)n / sizeof(events[0]);
        for (size_t i = 0; i < count; i++)
            handle_event(ed, &events[i]);
    }
    return 0;
}

static void update_caps(struct ycomp_server *s)
{
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (s->kb_group) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s->seat, caps);
}

void ycomp_evdev_init(struct ycomp_server *s)
{
    /* Повторные сканирования не должны открывать одно и то же устройство
     * дважды: помним st_rdev уже открытых узлов. */
    static dev_t opened[EVDEV_MAX_PATH];
    static int num_opened = 0;

    int found = 0;

    for (int i = 0; i < EVDEV_MAX_PATH; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        struct stat st;
        if (fstat(fd, &st) != 0) { close(fd); continue; }

        int seen = 0;
        for (int j = 0; j < num_opened; j++)
            if (opened[j] == st.st_rdev) { seen = 1; break; }
        if (seen) { close(fd); continue; }

        struct ycomp_evdev *ed = calloc(1, sizeof(*ed));
        if (!ed) { close(fd); continue; }

        ed->server = s;
        ed->fd = fd;
        probe_device(ed, path);

        if (!ed->is_keyboard && !ed->is_pointer) {
            close(fd);
            free(ed);
            continue;
        }

        char name[128] = "evdev";
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
            snprintf(name, sizeof(name), "%s", path);
        name[sizeof(name) - 1] = '\0';

        ed->source = wl_event_loop_add_fd(s->event_loop, fd, WL_EVENT_READABLE,
                                          evdev_dispatch, ed);
        if (!ed->source) {
            close(fd);
            free(ed);
            continue;
        }

        if (ed->is_keyboard) {
            wlr_keyboard_init(&ed->keyboard, &evdev_keyboard_impl, name);
            ycomp_keyboard_add(s, &ed->keyboard);
        }

        if (num_opened < EVDEV_MAX_PATH)
            opened[num_opened++] = st.st_rdev;

        fprintf(stderr, "ycomp: evdev %s \"%s\" (%s%s)\n", path, name,
                ed->is_keyboard ? "keyboard" : "",
                ed->is_pointer ? (ed->is_absolute ? " absolute pointer"
                                                 : " pointer") : "");
        found++;
    }

    if (found)
        update_caps(s);
}
