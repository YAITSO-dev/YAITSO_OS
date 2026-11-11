#ifndef YTERM_H
#define YTERM_H

#include "../libywt/libywt.h"
#include <stdint.h>
#include <stddef.h>

#define YT_COLS_MAX     512
#define YT_ROWS_MAX     256
#define YT_HIST_MAX     2000
#define YT_CELL_W       8
#define YT_CELL_H       16
#define YT_MAX_TABS     16
#define YT_SEARCH_MAX   128

#define YT_COLOR_FG_DEF 0xffd8dee9
#define YT_COLOR_BG_DEF 0xff2e3440
#define YT_COLOR_CUR    0xff88c0d0

enum {
    YT_ATTR_NONE   = 0,
    YT_ATTR_BOLD   = 1 << 0,
    YT_ATTR_ITALIC = 1 << 1,
    YT_ATTR_UNDER  = 1 << 2,
    YT_ATTR_REV    = 1 << 3,
    YT_ATTR_DIM    = 1 << 4,
};

struct yt_cell {
    uint32_t cp;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
};

struct yt_line {
    struct yt_cell cells[YT_COLS_MAX];
    int            len;
};

struct yt_term {
    int              cols, rows;
    int              cx, cy;
    struct yt_line   screen[YT_ROWS_MAX];
    struct yt_line   hist[YT_HIST_MAX];
    int              hist_head;
    int              hist_count;
    int              scroll_off;

    int              cursor_visible;
    uint32_t         cur_fg, cur_bg;
    uint16_t         cur_attrs;

    int              state;
    int              csi_args[16];
    int              csi_nargs;
    int              csi_cur;
    int              csi_priv;
    int              osc_len;
    char             osc_buf[256];

    int              saved_x, saved_y;

    /* Незавершённая UTF-8 последовательность между вызовами feed(). */
    uint32_t         utf8_cp;
    int              utf8_need;
    int              utf8_have;

    int              bracketed_paste;
    int              bell;
};

struct yt_tab {
    struct yt_term  term;
    int             master_fd;
    int             alive;
    int             has_bell;
    char            title[64];
    int             id;
};

struct yt_session {
    struct yt_tab   tabs[YT_MAX_TABS];
    int             ntabs;
    int             active;
    int             next_id;
};

struct yt_search {
    int   active;
    char  query[YT_SEARCH_MAX];
    int   qlen;
    int   matches[YT_ROWS_MAX];
    int   nmatch;
    int   cur_match;
    int   from_hist;
};

int  yt_pty_spawn(struct yt_term *t, char *const argv[], int *master_fd);
void yt_pty_resize(int master_fd, int cols, int rows);

void yt_term_init(struct yt_term *t, int cols, int rows);
void yt_term_resize(struct yt_term *t, int cols, int rows);
void yt_term_feed(struct yt_term *t, const char *buf, size_t n);
void yt_term_render(struct yt_term *t, ywt_buf_t *b, ywt_font_t *f);

void yt_term_scroll_up(struct yt_term *t);
void yt_term_scroll_down(struct yt_term *t);
int  yt_term_scroll_history(struct yt_term *t, int delta);

void yt_session_init(struct yt_session *s, int cols, int rows);
int  yt_session_new_tab(struct yt_session *s, char *const argv[],
                        int cols, int rows);
void yt_session_close_tab(struct yt_session *s, int idx);
void yt_session_switch(struct yt_session *s, int idx);

void yt_search_start(struct yt_search *s);
void yt_search_stop(struct yt_search *s);
void yt_search_input(struct yt_search *s, struct yt_term *t, uint32_t cp);
void yt_search_find(struct yt_search *s, struct yt_term *t);
void yt_search_next(struct yt_search *s, struct yt_term *t, int dir);

void yt_bell_notify(const char *tab_title);

#endif
