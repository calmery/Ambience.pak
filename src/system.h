/* NextUI / device integration: accent colour and battery status. */
#ifndef AMBIENCE_SYSTEM_H
#define AMBIENCE_SYSTEM_H

#include <SDL2/SDL.h>

/* Primary Accent Color, read from NextUI's settings (fallback #646464). */
extern SDL_Color g_accent;

void sys_load_accent(void);
void sys_battery(int *pct, int *charging);   /* pct < 0 when unavailable */
int  sys_volume(void);                       /* master volume 0-100, or < 0 if unavailable */
void sys_backlight(int on);                  /* screen backlight off/on (no-op off device) */

/* Power button -> suspend-to-RAM. NextUI's launcher is blocked while a pak
 * runs and nothing else handles the power key, so we read it ourselves and
 * suspend the device. All no-ops off device. */
void sys_power_init(void);                   /* open the input devices       */
int  sys_power_pressed(void);                /* 1 if power was pressed        */
void sys_power_drain(void);                  /* discard pending input         */
void sys_suspend(void);                      /* 2-stage sleep; returns after wake */

/* Screen-off mode: display + LEDs off, device stays awake (audio continues).
 * sys_screen_on restores backlight + LEDs to their pre-off state. */
void sys_screen_off(void);
void sys_screen_on(void);

/* Persistent data locations (outside the pak, so they survive uninstall).
 * On device: <SDCARD>/Ambience/{sounds,ambience.cfg}; bundled samples are
 * copied into the sounds dir on first run. Off device: local res/sounds and
 * ambience.cfg for desktop development. Returned pointers are static. */
const char *sys_sounds_dir(void);
const char *sys_config_path(void);

/* Stamp the running version into <SDCARD>/Ambience/version.txt (cwd off device)
 * so the installed build can be identified externally. Overwritten each launch. */
void sys_write_version(const char *ver);

#endif /* AMBIENCE_SYSTEM_H */
