#ifndef YBAR_H
#define YBAR_H

#include "../libywt/libywt.h"
#include <stdint.h>

#define YBAR_HEIGHT  28
#define YBAR_MAX_WIN 32

struct ybar_state {
    char     clock[16];
    char     date[24];
    char     ip[64];
    int      battery;
    int      has_battery;
    int      nwin;
    int      focus_id;
    char     wins[YBAR_MAX_WIN][96];
    int      win_id[YBAR_MAX_WIN];
    int      win_hidden[YBAR_MAX_WIN];
};

void ybar_widgets_update(struct ybar_state *s);
int  ybar_ipc_list(struct ybar_state *s);
int  ybar_ipc_send(const char *cmd, char *reply, size_t rn);

#endif
