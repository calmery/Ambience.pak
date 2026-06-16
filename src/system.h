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

/* Persistent data locations (outside the pak, so they survive uninstall).
 * On device: <SDCARD>/Ambience/{sounds,ambience.cfg}; bundled samples are
 * copied into the sounds dir on first run. Off device: local res/sounds and
 * ambience.cfg for desktop development. Returned pointers are static. */
const char *sys_sounds_dir(void);
const char *sys_config_path(void);

#endif /* AMBIENCE_SYSTEM_H */
