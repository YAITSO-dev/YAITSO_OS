#include "ybar.h"
#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void ybar_widgets_update(struct ybar_state *s)
{
    struct ifaddrs *ifa = NULL;
    s->ip[0] = 0;
    if (getifaddrs(&ifa) == 0) {
        for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr) continue;
            if (p->ifa_addr->sa_family != AF_INET) continue;
            if (!strcmp(p->ifa_name, "lo")) continue;
            struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
            snprintf(s->ip, sizeof(s->ip), "%s", inet_ntoa(sin->sin_addr));
            break;
        }
        freeifaddrs(ifa);
    }

    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) {
            s->battery = v;
            s->has_battery = 1;
        }
        fclose(f);
    } else {
        s->has_battery = 0;
    }
}
