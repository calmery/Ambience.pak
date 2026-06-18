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

void sys_write_version(const char *ver)
{
    resolve_paths();
    char path[600];
    snprintf(path, sizeof path, "%s/version.txt", s_base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s\n", ver ? ver : ""); fclose(f); }
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

/* ---- LED control (button lights on TrimUI Brick) -------------------- *
 * The Brick has four LED groups (f1, f2, m, lr) controlled via sysfs at
 * /sys/class/led_anim/.  We mirror NextUI's sleep sequence: save the
 * current state, switch to a breathe animation, then restore on wake.
 * Off-device (no sysfs nodes) every write silently fails → no-op. */
static const char *led_names[] = { "f1", "f2", "m", "lr" };
#define N_LEDS 4

static void write_sysfs(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val, strlen(val)); close(fd); }
}

static int read_sysfs_int(const char *path)
{
    return read_int_file(path);
}

static const char *brightness_path(const char *name)
{
    static char buf[128];
    if (!strcmp(name, "m"))  { snprintf(buf, sizeof buf, "/sys/class/led_anim/max_scale"); }
    else if (!strcmp(name, "f1") || !strcmp(name, "f2"))
        snprintf(buf, sizeof buf, "/sys/class/led_anim/max_scale_f1f2");
    else
        snprintf(buf, sizeof buf, "/sys/class/led_anim/max_scale_%s", name);
    return buf;
}

struct led_state {
    int brightness;     /* max_scale value (read from sysfs) */
    int inbrightness;   /* inbrightness from default (100) */
    int effect;
    int cycles;
    int speed;
    char color[16];     /* hex colour string read from sysfs */
};

static struct led_state led_saved[N_LEDS];

static void read_sysfs_str(const char *path, char *buf, int sz)
{
    buf[0] = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n > 0) { buf[n] = 0; while (n > 0 && buf[n-1] == '\n') buf[--n] = 0; }
}

static void leds_save(void)
{
    for (int i = 0; i < N_LEDS; i++) {
        char p[128];
        led_saved[i].brightness = read_sysfs_int(brightness_path(led_names[i]));
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_%s", led_names[i]);
        led_saved[i].effect = read_sysfs_int(p);
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_cycles_%s", led_names[i]);
        led_saved[i].cycles = read_sysfs_int(p);
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_duration_%s", led_names[i]);
        led_saved[i].speed = read_sysfs_int(p);
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_rgb_hex_%s", led_names[i]);
        read_sysfs_str(p, led_saved[i].color, sizeof led_saved[i].color);
    }
}

static int is_f2(int i) { return !strcmp(led_names[i], "f2"); }

/* Mirror CFW's LEDS_updateLeds(indicator_only=true) with LIGHT_PROFILE_SLEEP.
 * Write order: inbrightness → cycles → speed → color → effect (last = apply). */
static void leds_set_breathe(void)
{
    for (int i = 0; i < N_LEDS; i++) {
        char p[128], v[16];
        /* inbrightness (default 100) → max_scale; skip f2 (shares f1's path) */
        if (!is_f2(i))
            write_sysfs(brightness_path(led_names[i]), "100");
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_cycles_%s", led_names[i]);
        write_sysfs(p, "5");
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_duration_%s", led_names[i]);
        snprintf(v, sizeof v, "%d", led_saved[i].speed > 0 ? led_saved[i].speed : 1000);
        write_sysfs(p, v);
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_rgb_hex_%s", led_names[i]);
        write_sysfs(p, led_saved[i].color[0] ? led_saved[i].color : "FFFFFF");
        snprintf(p, sizeof p, "/sys/class/led_anim/effect_%s", led_names[i]);
        write_sysfs(p, "2");  /* 2 = breathe; writing effect last applies all */
    }
}

static void leds_off(void)
{
    for (int i = 0; i < N_LEDS; i++) {
        if (is_f2(i)) continue;                    /* f2 shares f1's brightness path */
        write_sysfs(brightness_path(led_names[i]), "0");
    }
}

/* Restore: brightness → cycles → speed → color → effect (last = apply). */
static void leds_restore(void)
{
    for (int i = 0; i < N_LEDS; i++) {
        char p[128], v[16];
        if (!is_f2(i) && led_saved[i].brightness >= 0) {
            snprintf(v, sizeof v, "%d", led_saved[i].brightness);
            write_sysfs(brightness_path(led_names[i]), v);
        }
        if (led_saved[i].cycles >= 0) {
            snprintf(p, sizeof p, "/sys/class/led_anim/effect_cycles_%s", led_names[i]);
            snprintf(v, sizeof v, "%d", led_saved[i].cycles);
            write_sysfs(p, v);
        }
        if (led_saved[i].speed >= 0) {
            snprintf(p, sizeof p, "/sys/class/led_anim/effect_duration_%s", led_names[i]);
            snprintf(v, sizeof v, "%d", led_saved[i].speed);
            write_sysfs(p, v);
        }
        if (led_saved[i].color[0]) {
            snprintf(p, sizeof p, "/sys/class/led_anim/effect_rgb_hex_%s", led_names[i]);
            write_sysfs(p, led_saved[i].color);
        }
        if (led_saved[i].effect >= 0) {
            snprintf(p, sizeof p, "/sys/class/led_anim/effect_%s", led_names[i]);
            snprintf(v, sizeof v, "%d", led_saved[i].effect);
            write_sysfs(p, v);
        }
    }
}

/* ---- power button -> suspend --------------------------------------- *
 * Detect the power key via SDL events (SDL_KEYUP scancode 102 or
 * SDL_JOYBUTTONUP button 102), matching CFW's PLAT_shouldWake / PAD_poll.
 * Raw evdev reads didn't work on the device — SDL owns the input layer. */
#define CODE_POWER  102
#define JOY_POWER   102

void sys_power_init(void) { /* nothing to do — SDL handles input */ }

static int poll_power(int drain_only)
{
    SDL_Event ev;
    int hit = 0;
    while (SDL_PollEvent(&ev)) {
        if (drain_only) continue;
        if (ev.type == SDL_KEYUP && ev.key.keysym.scancode == CODE_POWER)
            hit = 1;
        else if (ev.type == SDL_JOYBUTTONUP && ev.jbutton.button == JOY_POWER)
            hit = 1;
    }
    return hit;
}

int  sys_power_pressed(void) { return poll_power(0); }
void sys_power_drain(void)   { poll_power(1); }

/* suspend-to-RAM (deep sleep); blocks until the device wakes */
static void deep_sleep(void)
{
    leds_off();
    for (int i = 0; i < 5; i++) {
        int fd = open("/sys/power/state", O_WRONLY);
        if (fd < 0) { SDL_Delay(200); continue; }
        ssize_t r = write(fd, "mem", 3);           /* blocks until resume */
        close(fd);
        if (r >= 0) break;
        SDL_Delay(2000);                           /* can be EBUSY just after resume */
    }
}

/* check for power-button release (matching CFW's PLAT_shouldWake) */
static int wake_pressed(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYUP && ev.key.keysym.scancode == CODE_POWER)
            return 1;
        if (ev.type == SDL_JOYBUTTONUP && ev.jbutton.button == JOY_POWER)
            return 1;
    }
    return 0;
}

#define DEEP_SLEEP_DELAY 30000   /* ms before suspend-to-RAM (CFW default) */

void sys_suspend(void)
{
    leds_save();
    leds_set_breathe();                            /* breathe animation */
    sys_backlight(0);

    Uint32 start = SDL_GetTicks();
    int woken = 0;
    while (!woken) {
        SDL_Delay(200);
        if (wake_pressed()) { woken = 1; break; }
        if (SDL_GetTicks() - start >= DEEP_SLEEP_DELAY) {
            deep_sleep();                          /* suspend-to-RAM */
            woken = 1;                             /* resumed by power button */
        }
    }

    /* Wait for the power button to be fully released before continuing.
     * After resume, the user may still be holding the button; without this
     * drain loop the release event would arrive in the main loop and
     * immediately re-trigger sleep (matching CFW's PAD_reset behaviour). */
    for (int w = 0; w < 10; w++) {                 /* up to ~200ms */
        sys_power_drain();
        SDL_Delay(20);
    }
    sys_power_drain();

    sys_backlight(1);
    leds_restore();
}

void sys_screen_off(void)
{
    leds_save();
    leds_set_breathe();
    sys_backlight(0);
}

void sys_screen_on(void)
{
    sys_backlight(1);
    leds_restore();
}
