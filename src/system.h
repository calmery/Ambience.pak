/* NextUI / device integration: accent colour and battery status. */
#ifndef AMBIENCE_SYSTEM_H
#define AMBIENCE_SYSTEM_H

#include <SDL2/SDL.h>

/* Primary Accent Color, read from NextUI's settings (fallback #646464). */
extern SDL_Color g_accent;

void sys_load_accent(void);
void sys_battery(int *pct, int *charging);   /* pct < 0 when unavailable */

#endif /* AMBIENCE_SYSTEM_H */
