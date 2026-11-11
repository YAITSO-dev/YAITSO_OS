#define _GNU_SOURCE
#include "libytui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <errno.h>
#include <poll.h>

static struct termios g_saved;
static int            g_raw = 0;
static int            g_cursor_hidden = 0;

void ytui_init(void)
{
    if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return;
    struct termios raw = g_saved;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) g_raw = 1;
    fputs("\033[?25l", stdout);
    g_cursor_hidden = 1;
}

void ytui_shutdown(void)
{
    if (g_cursor_hidden) fputs("\033[?25h", stdout);
    fputs("\033[0m", stdout);
    fflush(stdout);
    if (g_raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
    g_raw = 0;
}

void ytui_clear(void)   { fputs("\033[2J\033[H", stdout); fflush(stdout); }
void ytui_refresh(void) { fflush(stdout); }

int ytui_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) return ws.ws_col;
    return 80;
}

int ytui_height(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) return ws.ws_row;
    return 24;
}

void ytui_gotoxy(int x, int y)
{
    printf("\033[%d;%dH", y + 1, x + 1);
}

void ytui_set_color(int fg, int bg, int bold)
{
    /* bg задаётся кодом цвета текста (30..37) и превращается в 40..47. */
    if (bg) printf("\033[%d;%d;%dm", bold ? 1 : 0, fg, bg + 10);
    else    printf("\033[%d;%dm", bold ? 1 : 0, fg);
}

void ytui_reset_color(void) { fputs("\033[0m", stdout); }
void ytui_hide_cursor(void) { fputs("\033[?25l", stdout); g_cursor_hidden = 1; }
void ytui_show_cursor(void) { fputs("\033[?25h", stdout); g_cursor_hidden = 0; }

void ytui_putc(char c)   { putchar(c); }
void ytui_puts(const char *s) { fputs(s, stdout); }

void ytui_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void ytui_hline(int x, int y, int len, char ch)
{
    ytui_gotoxy(x, y);
    for (int i = 0; i < len; i++) putchar(ch);
}

void ytui_vline(int x, int y, int len, char ch)
{
    for (int i = 0; i < len; i++) {
        ytui_gotoxy(x, y + i);
        putchar(ch);
    }
}

void ytui_box(int x, int y, int w, int h)
{
    ytui_gotoxy(x, y);
    putchar('+');
    for (int i = 0; i < w - 2; i++) putchar('-');
    putchar('+');
    for (int j = 1; j < h - 1; j++) {
        ytui_gotoxy(x, y + j);
        putchar('|');
        ytui_gotoxy(x + w - 1, y + j);
        putchar('|');
    }
    ytui_gotoxy(x, y + h - 1);
    putchar('+');
    for (int i = 0; i < w - 2; i++) putchar('-');
    putchar('+');
}

void ytui_fill(int x, int y, int w, int h, char ch)
{
    for (int j = 0; j < h; j++) {
        ytui_gotoxy(x, y + j);
        for (int i = 0; i < w; i++) putchar(ch);
    }
}

int ytui_read_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return YTUI_KEY_NONE;

    if (c == 0x1b) {
        /* Не блокируемся навсегда, если ESC нажат отдельно. */
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&pf, 1, 50) <= 0) return YTUI_KEY_ESC;
        unsigned char seq0;
        if (read(STDIN_FILENO, &seq0, 1) != 1) return YTUI_KEY_ESC;
        if (seq0 == '[') {
            if (poll(&pf, 1, 50) <= 0) return YTUI_KEY_ESC;
            unsigned char seq1;
            if (read(STDIN_FILENO, &seq1, 1) != 1) return YTUI_KEY_ESC;
            if (seq1 == 'A') return YTUI_KEY_UP;
            if (seq1 == 'B') return YTUI_KEY_DOWN;
            if (seq1 == 'C') return YTUI_KEY_RIGHT;
            if (seq1 == 'D') return YTUI_KEY_LEFT;
        }
        return YTUI_KEY_ESC;
    }
    if (c == '\n' || c == '\r') return YTUI_KEY_ENTER;
    if (c == '\t')              return YTUI_KEY_TAB;
    if (c == 0x7f || c == 0x08) return YTUI_KEY_BACKSPACE;
    if (c == 0x03)              return YTUI_KEY_CTRL_C;
    return (int)c;
}

int ytui_read_line(char *buf, size_t n, int x, int y)
{
    size_t len = 0;
    buf[0] = 0;
    ytui_gotoxy(x, y);
    ytui_show_cursor();

    for (;;) {
        int key = ytui_read_key();
        if (key == YTUI_KEY_ENTER)  break;
        if (key == YTUI_KEY_ESC)    { buf[0] = 0; return -1; }
        if (key == YTUI_KEY_CTRL_C) { buf[0] = 0; return -1; }
        if (key == YTUI_KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                buf[len] = 0;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }
        if (key >= 32 && key < 127 && len + 1 < n) {
            buf[len++] = (char)key;
            buf[len] = 0;
            putchar((char)key);
            fflush(stdout);
        }
    }
    ytui_hide_cursor();
    return (int)len;
}
