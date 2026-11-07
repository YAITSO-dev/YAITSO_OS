#ifndef LIBYTUI_H
#define LIBYTUI_H

#include <stdint.h>
#include <stddef.h>

#define YTUI_KEY_NONE      0
#define YTUI_KEY_UP        1
#define YTUI_KEY_DOWN      2
#define YTUI_KEY_LEFT      3
#define YTUI_KEY_RIGHT     4
#define YTUI_KEY_ENTER     5
#define YTUI_KEY_ESC       6
#define YTUI_KEY_TAB       7
#define YTUI_KEY_BACKSPACE 8
#define YTUI_KEY_CTRL_C    9

#define YTUI_COLOR_DEFAULT 0
#define YTUI_COLOR_BLACK   30
#define YTUI_COLOR_RED     31
#define YTUI_COLOR_GREEN   32
#define YTUI_COLOR_YELLOW  33
#define YTUI_COLOR_BLUE    34
#define YTUI_COLOR_MAGENTA 35
#define YTUI_COLOR_CYAN    36
#define YTUI_COLOR_WHITE   37

void  ytui_init(void);
void  ytui_shutdown(void);
void  ytui_clear(void);
void  ytui_refresh(void);
int   ytui_width(void);
int   ytui_height(void);

void  ytui_gotoxy(int x, int y);
void  ytui_set_color(int fg, int bg, int bold);
void  ytui_reset_color(void);
void  ytui_hide_cursor(void);
void  ytui_show_cursor(void);

void  ytui_putc(char c);
void  ytui_puts(const char *s);
void  ytui_printf(const char *fmt, ...);
void  ytui_hline(int x, int y, int len, char ch);
void  ytui_vline(int x, int y, int len, char ch);
void  ytui_box(int x, int y, int w, int h);
void  ytui_fill(int x, int y, int w, int h, char ch);

int   ytui_read_key(void);
int   ytui_read_line(char *buf, size_t n, int x, int y);

#endif
