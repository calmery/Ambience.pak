/* Rendering: fonts, primitives, scroll arrows, and the app screens. */
#ifndef AMBIENCE_UI_H
#define AMBIENCE_UI_H

#include <SDL2/SDL.h>
#include "app.h"

int  ui_load_fonts(void);                 /* 0 ok, -1 if font missing */
void ui_close_fonts(void);
void ui_load_arrows(SDL_Renderer *ren);   /* generate scroll-arrow textures */

void ui_render(SDL_Renderer *ren, App *a);
void ui_render_confirm(SDL_Renderer *ren);

/* Truncate a name to the display label (16 columns, multibyte = 2). */
void ui_make_disp(const char *src, char *out, int outsz);

#endif /* AMBIENCE_UI_H */
