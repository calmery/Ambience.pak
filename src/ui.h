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

/* Modal preset menu; returns one of UI_PM_* (own event loop, blocks). */
enum { UI_PM_CANCEL = -1, UI_PM_NEW = 0, UI_PM_RENAME, UI_PM_DELETE };
int ui_preset_menu(SDL_Renderer *ren, App *a);

/* Modal on-screen keyboard editing buf in place; 1 = confirmed, 0 = cancelled. */
int ui_keyboard(SDL_Renderer *ren, const char *title, char *buf, int bufsz);

void ui_keyboard_demo(SDL_Renderer *ren);   /* dev: one keyboard frame (--shotkb) */

/* Loading screen: progress bar + current file name. */
void ui_render_loading(SDL_Renderer *ren, int cur, int total, const char *name);

#endif /* AMBIENCE_UI_H */
