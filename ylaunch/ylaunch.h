#ifndef YLAUNCH_H
#define YLAUNCH_H

#include "../libywt/libywt.h"

#define YL_MAX_APPS 512

struct yl_app {
    char name[96];
    char exec[256];
    char icon[128];
    int  terminal;
};

int  yl_scan(struct yl_app *apps, int max);
void yl_launch(const struct yl_app *a);

#endif
