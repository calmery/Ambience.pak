#include "system.h"
#include <stdio.h>
#include <stdlib.h>

SDL_Color g_accent = {0x64, 0x64, 0x64, 255};

static int read_int_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

void sys_load_accent(void)
{
    const char *base = getenv("SDCARD_PATH");
    if (!base || !*base) base = "/mnt/SDCARD";
    char path[512];
    snprintf(path, sizeof path, "%s/.userdata/shared/minuisettings.txt", base);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    unsigned int c;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "color2=%x", &c) == 1) {     /* COLOR_ACCENT */
            g_accent = (SDL_Color){(c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255};
            break;
        }
    }
    fclose(f);
}

void sys_battery(int *pct, int *charging)
{
    static int cpct = -1, cchg = 0;
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    if (now >= next) {
        next = now + 5000;
        cpct = read_int_file("/sys/class/power_supply/axp2202-battery/capacity");
        cchg = (read_int_file("/sys/class/power_supply/axp2202-usb/online") == 1);
    }
    *pct = cpct;
    *charging = cchg;
}
