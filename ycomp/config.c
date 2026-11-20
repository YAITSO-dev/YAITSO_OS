#include "ycomp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static char g_terminal[128] = "yterm";
static char g_launcher[128] = "ylaunch";

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' ||
                     e[-1] == ' '  || e[-1] == '\t')) *--e = 0;
    return s;
}

static xkb_keysym_t parse_sym(const char *s)
{
    if (!strcmp(s, "Q"))         return XKB_KEY_q;
    if (!strcmp(s, "D"))         return XKB_KEY_d;
    if (!strcmp(s, "M"))         return XKB_KEY_m;
    if (!strcmp(s, "H"))         return XKB_KEY_h;
    if (!strcmp(s, "F"))         return XKB_KEY_f;
    if (!strcmp(s, "Left"))      return XKB_KEY_Left;
    if (!strcmp(s, "Right"))     return XKB_KEY_Right;
    if (!strcmp(s, "Up"))        return XKB_KEY_Up;
    if (!strcmp(s, "Down"))      return XKB_KEY_Down;
    return xkb_keysym_from_name(s, XKB_KEYSYM_CASE_INSENSITIVE);
}

static void parse_hotkey(const char *val, xkb_keysym_t *sym, int *mods)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", val);
    *sym = 0;
    *mods = 0;
    char *p = buf;
    while (*p) {
        char *plus = strchr(p, '+');
        if (plus) *plus = 0;
        char *tok = trim(p);
        if (!strcmp(tok, "Super") || !strcmp(tok, "Mod4")) *mods |= 1;
        else if (!strcmp(tok, "Alt"))   *mods |= 2;
        else if (!strcmp(tok, "Ctrl") ||
                 !strcmp(tok, "Control")) *mods |= 4;
        else if (!strcmp(tok, "Shift")) *mods |= 8;
        else *sym = parse_sym(tok);
        if (!plus) break;
        p = plus + 1;
    }
}

/* #rrggbb или #rrggbbaa. */
static int parse_color(const char *v, float out[4])
{
    if (!v || *v != '#') return -1;
    v++;
    size_t n = strlen(v);
    if (n != 6 && n != 8) return -1;
    unsigned int c[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < n / 2; i++) {
        unsigned int b = 0;
        for (int k = 0; k < 2; k++) {
            char ch = v[i * 2 + (size_t)k];
            int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else return -1;
            b = b * 16 + (unsigned int)d;
        }
        c[i] = b;
    }
    out[0] = (float)c[0] / 255.0f;
    out[1] = (float)c[1] / 255.0f;
    out[2] = (float)c[2] / 255.0f;
    out[3] = (float)c[3] / 255.0f;
    return 0;
}

void ycomp_config_load(struct ycomp_server *s)
{
    s->hk_terminal = XKB_KEY_Return; s->hk_terminal_mods = 1;
    s->hk_launcher = XKB_KEY_d;      s->hk_launcher_mods = 1;
    s->hk_close    = XKB_KEY_q;      s->hk_close_mods    = 1;
    s->hk_exit     = XKB_KEY_q;      s->hk_exit_mods     = 1 | 8;
    s->hk_maximize = XKB_KEY_m;      s->hk_maximize_mods = 1;
    s->hk_restore  = XKB_KEY_Down;   s->hk_restore_mods  = 1;
    s->hk_snap_left = XKB_KEY_Left;  s->hk_snap_left_mods = 1;
    s->hk_snap_right = XKB_KEY_Right; s->hk_snap_right_mods = 1;
    s->hk_minimize = XKB_KEY_h;      s->hk_minimize_mods = 1;
    s->hk_next     = XKB_KEY_Tab;    s->hk_next_mods     = 1;
    s->hk_prev     = XKB_KEY_Tab;    s->hk_prev_mods     = 1 | 8;
    s->bar_height  = YCOMP_BAR_DEF;

    s->bg_top[0] = 0.06f; s->bg_top[1] = 0.08f; s->bg_top[2] = 0.11f;
    s->bg_top[3] = 1.0f;
    s->bg_bottom[0] = 0.14f; s->bg_bottom[1] = 0.18f;
    s->bg_bottom[2] = 0.23f; s->bg_bottom[3] = 1.0f;

    FILE *f = fopen("/etc/yaitso/desktop.conf", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '[' || !*p) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p);
        char *v = trim(eq + 1);
        if (!strcmp(k, "terminal")) {
            snprintf(g_terminal, sizeof(g_terminal), "%s", v);
        } else if (!strcmp(k, "launcher")) {
            snprintf(g_launcher, sizeof(g_launcher), "%s", v);
        } else if (!strcmp(k, "hk_terminal")) {
            parse_hotkey(v, &s->hk_terminal, &s->hk_terminal_mods);
        } else if (!strcmp(k, "hk_launcher")) {
            parse_hotkey(v, &s->hk_launcher, &s->hk_launcher_mods);
        } else if (!strcmp(k, "hk_close")) {
            parse_hotkey(v, &s->hk_close, &s->hk_close_mods);
        } else if (!strcmp(k, "hk_exit")) {
            parse_hotkey(v, &s->hk_exit, &s->hk_exit_mods);
        } else if (!strcmp(k, "hk_maximize")) {
            parse_hotkey(v, &s->hk_maximize, &s->hk_maximize_mods);
        } else if (!strcmp(k, "hk_restore")) {
            parse_hotkey(v, &s->hk_restore, &s->hk_restore_mods);
        } else if (!strcmp(k, "hk_snap_left")) {
            parse_hotkey(v, &s->hk_snap_left, &s->hk_snap_left_mods);
        } else if (!strcmp(k, "hk_snap_right")) {
            parse_hotkey(v, &s->hk_snap_right, &s->hk_snap_right_mods);
        } else if (!strcmp(k, "hk_minimize")) {
            parse_hotkey(v, &s->hk_minimize, &s->hk_minimize_mods);
        } else if (!strcmp(k, "hk_next")) {
            parse_hotkey(v, &s->hk_next, &s->hk_next_mods);
        } else if (!strcmp(k, "hk_prev")) {
            parse_hotkey(v, &s->hk_prev, &s->hk_prev_mods);
        } else if (!strcmp(k, "bar_height")) {
            int h = atoi(v);
            if (h >= 16 && h <= 96) s->bar_height = h;
        } else if (!strcmp(k, "background")) {
            (void)parse_color(v, s->bg_top);
        } else if (!strcmp(k, "background_bottom")) {
            (void)parse_color(v, s->bg_bottom);
        }
    }
    fclose(f);
}

const char *ycomp_cfg_terminal(void) { return g_terminal; }
const char *ycomp_cfg_launcher(void) { return g_launcher; }
