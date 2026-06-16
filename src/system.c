#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

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

/* NextUI keeps all device settings in a POSIX shared-memory segment
 * ("/SharedSettings", a Settings struct of ints, created by keymon). We read
 * the live volume from it directly - no extra library needed. The layout is
 * version-locked (SETTINGS_VERSION); on any mismatch (or off-device) we just
 * report "unavailable" so nothing breaks. Mirrors libmsettings' GetVolume(). */
#define NEXTUI_SETTINGS_VERSION 10
#define NEXTUI_SETTINGS_INTS    29   /* sizeof(SettingsV10) / sizeof(int) */
#define MUTE_NO_CHANGE          (-69)

/* field indices into the int[] Settings struct (SettingsV10 layout) */
enum {
    ST_VERSION = 0, ST_HEADPHONES = 3, ST_SPEAKER = 4, ST_MUTE = 5,
    ST_TOGGLED_VOLUME = 14, ST_JACK = 27, ST_AUDIOSINK = 28,
};

int sys_volume(void)
{
    static int cvol = -1;
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    if (now < next) return cvol;
    next = now + 1000;
    cvol = -1;

    int fd = shm_open("/SharedSettings", O_RDONLY, 0);
    if (fd < 0) return cvol;
    size_t sz = NEXTUI_SETTINGS_INTS * sizeof(int);
    int *s = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED) return cvol;

    if (s[ST_VERSION] == NEXTUI_SETTINGS_VERSION) {
        int mute = s[ST_MUTE], toggled = s[ST_TOGGLED_VOLUME];
        int vol = (mute && toggled != MUTE_NO_CHANGE)        ? toggled
                : (s[ST_JACK] || s[ST_AUDIOSINK] != 0)       ? s[ST_HEADPHONES]
                                                             : s[ST_SPEAKER];
        if (vol < 0) vol = 0; if (vol > 20) vol = 20;
        cvol = vol * 5;                       /* 0-20 -> 0-100 % */
    }
    munmap(s, sz);
    return cvol;
}
