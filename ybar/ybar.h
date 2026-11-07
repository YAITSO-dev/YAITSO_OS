#ifndef YBAR_H
#define YBAR_H

#include "../libywt/libywt.h"
#include <stdint.h>

#define YBAR_HEIGHT  24
#define YBAR_MAX_WIN 32

struct ybar_state {
    char     clock[16];
    char     ip[64];
    int      battery;
    int      has_battery;
    int      nwin;
    int      focus_id;
    char     wins[YBAR_MAX_WIN][96];
};

void ybar_widgets_update(struct ybar_state *s);
int  ybar_ipc_list(char wins[][96], int max);
int  ybar_ipc_send(const char *cmd, char *reply, size_t rn);

#endif
