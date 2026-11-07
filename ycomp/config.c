#include "ycomp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    if (!strcmp(s, "Return") || !strcmp(s, "Enter")) return XKB_KEY_Return;
    if (!strcmp(s, "Escape"))    return XKB_KEY_Escape;
    if (!strcmp(s, "Tab"))       return XKB_KEY_Tab;
    if (!strcmp(s, "Space"))     return XKB_KEY_space;
    if (!strcmp(s, "Q"))         return XKB_KEY_q;
    if (!strcmp(s, "D"))         return XKB_KEY_d;
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

void ycomp_config_load(struct ycomp_server *s)
{
    s->hk_terminal = XKB_KEY_Return; s->hk_terminal_mods = 1;
    s->hk_launcher = XKB_KEY_d;      s->hk_launcher_mods = 1;
    s->hk_close    = XKB_KEY_q;      s->hk_close_mods    = 1;
    s->hk_exit     = XKB_KEY_q;      s->hk_exit_mods     = 1 | 8;

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
        }
    }
    fclose(f);
}

const char *ycomp_cfg_terminal(void) { return g_terminal; }
const char *ycomp_cfg_launcher(void) { return g_launcher; }
