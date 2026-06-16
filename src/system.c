#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>

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

/* ---- persistent data paths (survive pak uninstall) ----------------- */

static char s_base[512], s_sounds[600], s_config[640];

static void ensure_dir(const char *p) { mkdir(p, 0777); }   /* EEXIST is fine */

static int is_audio(const char *name)
{
    const char *e = strrchr(name, '.');
    return e && (!strcasecmp(e, ".mp3") || !strcasecmp(e, ".ogg") || !strcasecmp(e, ".wav"));
}

static int dir_has_audio(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e; int has = 0;
    while ((e = readdir(d))) if (is_audio(e->d_name)) { has = 1; break; }
    closedir(d);
    return has;
}

static void copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"); if (!in) return;
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return; }
    char buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
}

/* first run: copy bundled samples so there's something to play right away */
static void seed_sounds(const char *target, const char *bundled)
{
    if (dir_has_audio(target)) return;
    DIR *d = opendir(bundled);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!is_audio(e->d_name)) continue;
        char src[1024], dst[1024];
        snprintf(src, sizeof src, "%s/%s", bundled, e->d_name);
        snprintf(dst, sizeof dst, "%s/%s", target, e->d_name);
        copy_file(src, dst);
    }
    closedir(d);
}

static void resolve_paths(void)
{
    if (s_base[0]) return;
    const char *sd = getenv("SDCARD_PATH");
    if ((!sd || !*sd) && access("/mnt/SDCARD", F_OK) == 0) sd = "/mnt/SDCARD";
    if (sd && *sd) {                              /* on device: persist on the SD card */
        snprintf(s_base,   sizeof s_base,   "%s/Ambience", sd);
        ensure_dir(s_base);
        snprintf(s_sounds, sizeof s_sounds, "%s/sounds", s_base);
        ensure_dir(s_sounds);
        snprintf(s_config, sizeof s_config, "%s/ambience.cfg", s_base);
        /* one-time migration from older builds that wrote inside the pak */
        if (access(s_config, F_OK) != 0 && access("ambience.cfg", F_OK) == 0)
            copy_file("ambience.cfg", s_config);
        seed_sounds(s_sounds, "res/sounds");     /* bundled samples inside the pak */
    } else {                                      /* desktop dev: keep it local */
        strcpy(s_base, ".");
        strcpy(s_sounds, "res/sounds");
        strcpy(s_config, "ambience.cfg");
    }
}

const char *sys_sounds_dir(void)  { resolve_paths(); return s_sounds; }
const char *sys_config_path(void) { resolve_paths(); return s_config; }

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
    ST_VERSION = 0, ST_BRIGHTNESS = 1, ST_HEADPHONES = 3, ST_SPEAKER = 4,
    ST_MUTE = 5, ST_TOGGLED_VOLUME = 14, ST_JACK = 27, ST_AUDIOSINK = 28,
};

/* Copy the shared settings into out[NEXTUI_SETTINGS_INTS]; 1 on success. */
static int settings_snapshot(int *out)
{
    int fd = shm_open("/SharedSettings", O_RDONLY, 0);
    if (fd < 0) return 0;
    size_t sz = NEXTUI_SETTINGS_INTS * sizeof(int);
    int *s = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED) return 0;
    int ok = (s[ST_VERSION] == NEXTUI_SETTINGS_VERSION);
    if (ok) memcpy(out, s, sz);
    munmap(s, sz);
    return ok;
}

int sys_volume(void)
{
    static int cvol = -1;
    static Uint32 next = 0;
    Uint32 now = SDL_GetTicks();
    if (now < next) return cvol;
    next = now + 1000;
    cvol = -1;

    int s[NEXTUI_SETTINGS_INTS];
    if (settings_snapshot(s)) {
        int mute = s[ST_MUTE], toggled = s[ST_TOGGLED_VOLUME];
        int vol = (mute && toggled != MUTE_NO_CHANGE)        ? toggled
                : (s[ST_JACK] || s[ST_AUDIOSINK] != 0)       ? s[ST_HEADPHONES]
                                                             : s[ST_SPEAKER];
        if (vol < 0) vol = 0; if (vol > 20) vol = 20;
        cvol = vol * 5;                       /* 0-20 -> 0-100 % */
    }
    return cvol;
}

/* ---- backlight (sleep-timer screen off) ----------------------------- *
 * Borrow NextUI's own SetRawBrightness (the /dev/disp brightness ioctl) via
 * dlopen so we use the exact, correct call rather than guessing the ioctl. If
 * the library isn't found (off device) every call is a harmless no-op. */
static int is_brick(void) { const char *d = getenv("DEVICE"); return d && !strcmp(d, "brick"); }

/* mirror libmsettings' scaleBrightness (0-10 setting -> 0-255 raw) */
static int scale_brightness(int v)
{
    static const int brk[11] = {1, 8, 16, 32, 48, 72, 96, 128, 160, 192, 255};
    static const int reg[11] = {4, 6, 10, 16, 32, 48, 64, 96, 128, 192, 255};
    if (v < 0) v = 0; if (v > 10) v = 10;
    return is_brick() ? brk[v] : reg[v];
}

void sys_backlight(int on)
{
    static void (*set_raw)(int) = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        void *h = dlopen("libmsettings.so", RTLD_NOW | RTLD_GLOBAL);
        if (!h) h = dlopen("/usr/trimui/lib/libmsettings.so", RTLD_NOW);
        if (h) *(void **)&set_raw = dlsym(h, "SetRawBrightness");
    }
    if (!set_raw) return;                          /* unavailable: no-op */
    if (!on) { set_raw(0); return; }               /* off */

    int s[NEXTUI_SETTINGS_INTS];                   /* restore the user's brightness */
    int b = settings_snapshot(s) ? s[ST_BRIGHTNESS] : 5;
    set_raw(scale_brightness(b));
}
