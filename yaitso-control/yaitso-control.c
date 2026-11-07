#define _GNU_SOURCE
#include "../ytui/libytui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct item {
    const char *label;
    const char *cmd;
};

static struct item g_items[] = {
    {"Системная информация",    "uname -a; uptime"},
    {"Пакеты (yat list)",        "yat list"},
    {"Сервисы (svc list)",       "svc list"},
    {"WiFi (ywifi scan)",        "ywifi scan"},
    {"Сеть (ip addr)",           "ip addr"},
    {"Диск (df -h)",             "df -h"},
    {"Память (free)",            "free -h"},
    {"Процессы (ps)",            "ps aux"},
    {"Обновить индекс (yat update)", "yat update"},
    {"Обновить всё (yat upgrade)",   "yat upgrade"},
    {NULL, NULL}
};

static void draw_menu(int sel)
{
    ytui_clear();
    int w = ytui_width();
    int h = ytui_height();

    ytui_set_color(YTUI_COLOR_CYAN, 0, 1);
    ytui_gotoxy(2, 1);
    ytui_puts("Yaitso Control Panel");
    ytui_reset_color();

    ytui_gotoxy(2, 2);
    for (int i = 0; i < w - 4 && i < 40; i++) putchar('-');

    int y = 4;
    for (int i = 0; g_items[i].label; i++) {
        ytui_gotoxy(3, y + i);
        if (i == sel) {
            ytui_set_color(YTUI_COLOR_BLACK, YTUI_COLOR_CYAN, 1);
            ytui_printf(" > %-30s ", g_items[i].label);
            ytui_reset_color();
        } else {
            ytui_printf("   %-30s ", g_items[i].label);
        }
    }

    ytui_gotoxy(2, h - 2);
    ytui_set_color(YTUI_COLOR_YELLOW, 0, 0);
    ytui_puts("↑/↓ выб., Enter — запустить, q — выход");
    ytui_reset_color();
    ytui_refresh();
}

int main(void)
{
    ytui_init();
    int sel = 0;
    int nitems = 0;
    while (g_items[nitems].label) nitems++;

    for (;;) {
        draw_menu(sel);
        int k = ytui_read_key();
        if (k == 'q' || k == 'Q' || k == YTUI_KEY_CTRL_C) break;
        if (k == YTUI_KEY_UP)   { if (sel > 0) sel--; continue; }
        if (k == YTUI_KEY_DOWN) { if (sel + 1 < nitems) sel++; continue; }
        if (k == YTUI_KEY_ENTER) {
            ytui_shutdown();
            printf("\n>>> %s\n", g_items[sel].cmd);
            fflush(stdout);
            int r = system(g_items[sel].cmd);
            (void)r;
            printf("\nНажмите Enter...");
            fflush(stdout);
            getchar();
            ytui_init();
        }
    }

    ytui_shutdown();
    printf("\n");
    return 0;
}
