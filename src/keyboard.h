#ifndef OSK_KEYBOARD_H
#define OSK_KEYBOARD_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/*
 * On-screen keyboard - a self-contained modal text-entry widget for SDL2 +
 * SDL2_ttf apps driven by a gamepad/d-pad (NextUI / TrimUI controller layout)
 * or a hardware keyboard.
 *
 * Portable by design: this module has no dependency on the host app. Drop
 * keyboard.c / keyboard.h into any project, provide three fonts and an accent
 * colour, and call osk_run(). It ships its own drawing primitives and palette,
 * and reads the canvas size from the renderer, so it adapts to any resolution.
 *
 * Controls:
 *   D-pad / arrows  move (hold to repeat, nearest-key vertical navigation)
 *   A / on-screen   select the highlighted key
 *   Y               backspace (hold to repeat)
 *   X               space
 *   Start / Return  confirm (Done)
 *   B / Esc         cancel
 *
 * Features: iPhone-style ABC / 123 / #+= pages, one-shot and caps-lock shift
 * (double-tap), UTF-8 aware editing and length limit.
 */
typedef struct {
    TTF_Font *font_title;  /* screen title and the input field text (large) */
    TTF_Font *font_key;    /* key labels (medium)                          */
    TTF_Font *font_hint;   /* bottom hint bar (small)                      */
    SDL_Color accent;      /* highlight colour for the cursor / caps lock  */
    int       max_chars;   /* max code points; <= 0 means buffer-bounded   */
} OskConfig;

/* Modal text entry. `buf` holds the initial text and receives the edited
 * result (`bufsz` includes the NUL terminator). Blocks until the user confirms
 * or cancels. Returns 1 if confirmed, 0 if cancelled. */
int osk_run(SDL_Renderer *ren, const OskConfig *cfg,
            const char *title, char *buf, int bufsz);

/* Render one representative frame (no event loop) - handy for screenshots and
 * layout previews. */
void osk_demo(SDL_Renderer *ren, const OskConfig *cfg);

#endif /* OSK_KEYBOARD_H */
