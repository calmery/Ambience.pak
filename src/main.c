/*
 * Ambience - an ambient-sound mixer for NextUI handhelds.
 *
 * Drop audio files (.ogg / .wav / .mp3) into <SDCARD>/Ambience/sounds/; each
 * becomes a looping channel with its own volume and mute. Presets are saved to
 * <SDCARD>/Ambience/ambience.cfg. Both live outside the pak so they survive an
 * uninstall (see system.c for path resolution). See app.h for the module layout.
 *
 * Controls (keyboard / TrimUI):
 *   Up / Down       D-Pad U/D    select channel
 *   Left / Right    D-Pad L/R    adjust volume (hold to repeat)
 *   M / Enter       A            mute / unmute channel
 *   P / Space       X            play / pause
 *   T               Select       sleep timer (5-min steps, off..60m)
 *   Q / Esc         B            quit (with confirmation)
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "system.h"
#include "audio.h"
#include "ui.h"
#include "config.h"
#include "actions.h"

static App g_app;

static void app_seed(App *a)
{
    memset(a, 0, sizeof *a);
    a->master = 1.0f;
    a->fade_cur = 1.0f;
    a->fade_target = 1.0f;
    a->sleep_left = -1;
}

/* ---- headless selftest: scan + decode, no audio device ------------- */

static int run_selftest(void)
{
    app_seed(&g_app);
    audio_load_sounds(&g_app, sys_sounds_dir(), NULL);
    printf("selftest: channels=%d\n", g_app.nch);
    for (int c = 0; c < g_app.nch; c++)
        printf("  %-16s %d frames (%.1fs)\n", g_app.ch[c].name,
               g_app.ch[c].frames, g_app.ch[c].frames / (float)SR);
    audio_free_sounds(&g_app);
    return 0;
}

/* ---- offscreen screenshot (dev): render one UI frame to a BMP ------- */

static int run_shot(const char *path)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 32,
                                                    SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(s);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ui_load_fonts();
    ui_load_arrows(r);
    app_seed(&g_app);
    const char *names[] = {"Rain", "Ocean", "Fire", "Wind", "Forest",
        "VeryLongChannelName", "Thunder", "Brook", "とても長い名前のチャンネル",
        "Birds", "Cafe", "Train"};
    g_app.nch = 12;
    for (int i = 0; i < 12; i++) {
        strncpy(g_app.ch[i].name, names[i], sizeof g_app.ch[i].name - 1);
        ui_make_disp(g_app.ch[i].name, g_app.ch[i].disp, sizeof g_app.ch[i].disp);
        g_app.ch[i].target = 0.2f + 0.06f * i;
    }
    g_app.ch[2].muted = 1; g_app.ch[2].saved = 0.6f;
    g_app.sel = 6;
    const char *pn[] = {"Default", "Storm", "Focus"};
    g_app.npreset = 3; g_app.active = 1;
    for (int i = 0; i < 3; i++) strcpy(g_app.presets[i].name, pn[i]);
    ui_render(r, &g_app);
    SDL_RenderPresent(r);
    SDL_SaveBMP(s, path);
    printf("wrote %s\n", path);
    return 0;
}

static int run_shotload(const char *path)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 32,
                                                    SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(s);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ui_load_fonts();
    sys_load_accent();
    ui_render_loading(r, 3, 9, "Brown Noise");
    SDL_RenderPresent(r);
    SDL_SaveBMP(s, path);
    printf("wrote %s\n", path);
    return 0;
}

static int run_shotkb(const char *path)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 32,
                                                    SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(s);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ui_load_fonts();
    ui_keyboard_demo(r);
    SDL_RenderPresent(r);
    SDL_SaveBMP(s, path);
    printf("wrote %s\n", path);
    return 0;
}

/* ---- input mapping ------------------------------------------------- */

/* Map an SDL event to an action code (ACT_*), or -1. Sets *is_dir for the
 * d-pad/arrow directions (which auto-repeat on hold) and *is_release on key/
 * button up of a direction. TrimUI uses a Nintendo layout, so SDL names
 * buttons by Xbox position: physical A == SDL_B, physical X == SDL_Y, etc. */
static int map_event(const SDL_Event *e, int *is_release)
{
    *is_release = 0;
    if (e->type == SDL_KEYDOWN && e->key.repeat == 0) {
        switch (e->key.keysym.sym) {
        case SDLK_UP: return ACT_UP;
        case SDLK_DOWN: return ACT_DOWN;
        case SDLK_LEFT: return ACT_DEC;
        case SDLK_RIGHT: return ACT_INC;
        case SDLK_p: case SDLK_SPACE: return ACT_PAUSE;
        case SDLK_t: return ACT_SLEEP;
        case SDLK_q: case SDLK_ESCAPE: return ACT_QUIT;
        case SDLK_m: case SDLK_RETURN: return ACT_MUTE;
        }
    } else if (e->type == SDL_KEYUP) {
        switch (e->key.keysym.sym) {
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
            *is_release = 1; break;
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return ACT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ACT_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ACT_DEC;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ACT_INC;
        case SDL_CONTROLLER_BUTTON_Y: return ACT_PAUSE;     /* physical X */
        case SDL_CONTROLLER_BUTTON_BACK: return ACT_SLEEP;  /* Select     */
        case SDL_CONTROLLER_BUTTON_A: return ACT_QUIT;      /* physical B */
        case SDL_CONTROLLER_BUTTON_START: return ACT_QUIT;
        case SDL_CONTROLLER_BUTTON_B: return ACT_MUTE;      /* physical A */
        }
    } else if (e->type == SDL_CONTROLLERBUTTONUP) {
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            *is_release = 1; break;
        }
    }
    return -1;
}

/* Preset controls (need the renderer for the menu/keyboard, so handled here
 * rather than via act_apply). Returns 1 if the event was consumed. */
static int handle_preset(const SDL_Event *e, SDL_Renderer *ren, App *a)
{
    int prev = 0, next = 0, menu = 0;
    if (e->type == SDL_KEYDOWN && e->key.repeat == 0) {
        if (e->key.keysym.sym == SDLK_LEFTBRACKET) prev = 1;
        else if (e->key.keysym.sym == SDLK_RIGHTBRACKET) next = 1;
        else if (e->key.keysym.sym == SDLK_n) menu = 1;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) prev = 1;
        else if (e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) next = 1;
        else if (e->cbutton.button == SDL_CONTROLLER_BUTTON_X) menu = 1;  /* physical Y */
    } else if (e->type == SDL_JOYBUTTONDOWN) {
        if (e->jbutton.button == 8) menu = 1;   /* TrimUI MENU button (JOY_MENU) */
    }
    if (!prev && !next && !menu) return 0;

    if (prev) config_switch(a, (a->active + a->npreset - 1) % a->npreset);
    else if (next) config_switch(a, (a->active + 1) % a->npreset);
    else {
        int choice = ui_preset_menu(ren, a);
        char name[64];
        if (choice == UI_PM_NEW) {
            name[0] = 0;
            if (ui_keyboard(ren, "New preset", name, sizeof name) && name[0])
                config_new(a, name);
        } else if (choice == UI_PM_RENAME) {
            strncpy(name, a->presets[a->active].name, sizeof name - 1);
            name[sizeof name - 1] = 0;
            if (ui_keyboard(ren, "Rename preset", name, sizeof name) && name[0])
                config_rename(a, a->active, name);
        } else if (choice == UI_PM_DELETE) {
            config_delete(a, a->active);
        }
    }
    return 1;
}

/* In the quit-confirm modal: A confirms, B cancels. */
static int confirm_event(const SDL_Event *e, int *running)
{
    if (e->type == SDL_KEYDOWN && e->key.repeat == 0) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_RETURN || k == SDLK_y) { *running = 0; return 1; }
        if (k == SDLK_ESCAPE || k == SDLK_n || k == SDLK_b) return 1;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_B) { *running = 0; return 1; }
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_A) return 1;  /* cancel */
    }
    return 0;
}

/* ---- loading screen ------------------------------------------------ */

static SDL_Renderer *s_ren;

static void loading_progress(int cur, int total, const char *name)
{
    if (s_ren) ui_render_loading(s_ren, cur, total, name);
}

/* ---- main ---------------------------------------------------------- */

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) return run_selftest();
        if (!strcmp(argv[i], "--shot")) return run_shot(i + 1 < argc ? argv[i + 1] : "shot.bmp");
        if (!strcmp(argv[i], "--shotload")) return run_shotload(i + 1 < argc ? argv[i + 1] : "shotload.bmp");
        if (!strcmp(argv[i], "--shotkb")) return run_shotkb(i + 1 < argc ? argv[i + 1] : "shotkb.bmp");
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Ambience",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, UI_W, UI_H, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, UI_W, UI_H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    ui_load_fonts();
    ui_load_arrows(ren);

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

    sys_load_accent();
    sys_power_init();
    SDL_Log("accent = #%02x%02x%02x", g_accent.r, g_accent.g, g_accent.b);
    app_seed(&g_app);
    s_ren = ren;
    audio_load_sounds(&g_app, sys_sounds_dir(), loading_progress);
    s_ren = NULL;
    config_load(&g_app);
    config_apply_active(&g_app);
    if (audio_open(&g_app) != 0)
        SDL_Log("continuing without audio");   /* UI still works */

    int running = 1, confirm = 0;
    int screen_off = 0;                         /* sleep timer turned the screen off */
    int held = -1;                              /* held direction action */
    Uint32 held_since = 0, held_last = 0;
    const Uint32 HOLD_DELAY = 350, HOLD_RATE = 70;
    Uint32 last = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; continue; }

            /* power button release (SDL_KEYUP scancode 102 or joystick 102) */
            if ((e.type == SDL_KEYUP && e.key.keysym.scancode == 102) ||
                (e.type == SDL_JOYBUTTONUP && e.jbutton.button == 102)) {
                if (screen_off) { sys_screen_on(); screen_off = 0; }
                else             { sys_screen_off(); screen_off = 1; }
                for (int w = 0; w < 10; w++) { sys_power_drain(); SDL_Delay(20); }
                sys_power_drain();
                held = -1;
                last = SDL_GetTicks();
                break;
            }

            if (screen_off) {
                int release = 0;
                int code = map_event(&e, &release);
                if (release) held = -1;
                if (code == ACT_DEC || code == ACT_INC) {
                    act_apply(&g_app, code, &confirm);
                    if (code <= ACT_INC) { held = code; held_since = held_last = SDL_GetTicks(); }
                }
                continue;
            }
            if (confirm) {
                if (confirm_event(&e, &running)) confirm = 0;
                continue;
            }
            if (handle_preset(&e, ren, &g_app)) { held = -1; continue; }
            int release = 0;
            int code = map_event(&e, &release);
            if (release) held = -1;
            if (code >= 0) {
                act_apply(&g_app, code, &confirm);
                if (code <= ACT_INC) { held = code; held_since = held_last = SDL_GetTicks(); }
                if (confirm) held = -1;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (!confirm && held >= 0 &&
            now - held_since >= HOLD_DELAY && now - held_last >= HOLD_RATE) {
            act_apply(&g_app, held, &confirm);
            held_last = now;
        }

        float dt = (now - last) / 1000.0f;
        last = now;
        if (g_app.sleep_left >= 0 && !g_app.paused) {
            SDL_LockAudioDevice(g_dev);
            g_app.sleep_left -= dt;
            if (g_app.sleep_left < 0) g_app.sleep_left = 0;
            if (g_app.sleep_left <= 4.0f) g_app.fade_target = 0.0f;
            int expired = (g_app.sleep_left == 0 && g_app.fade_cur <= 0.001f);
            if (expired) { g_app.sleep_left = -1; g_app.paused = 1; }
            SDL_UnlockAudioDevice(g_dev);
            if (expired) {
                if (screen_off) { sys_screen_on(); screen_off = 0; }
                if (g_dev) SDL_PauseAudioDevice(g_dev, 1);
                sys_suspend();
                SDL_PumpEvents();
                SDL_FlushEvent(SDL_KEYDOWN); SDL_FlushEvent(SDL_KEYUP);
                SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN); SDL_FlushEvent(SDL_CONTROLLERBUTTONUP);
                held = -1;
                last = SDL_GetTicks();
            }
        }

        if (g_app.dirty && now - g_app.last_change > 800) config_save(&g_app);

        if (screen_off) {
            SDL_Delay(50);
            continue;
        }
        ui_render(ren, &g_app);
        if (confirm) ui_render_confirm(ren);
        SDL_RenderPresent(ren);
    }

    if (screen_off) sys_screen_on();             /* don't leave it dark on exit */
    if (g_app.dirty) config_save(&g_app);
    audio_close();
    audio_free_sounds(&g_app);
    ui_close_fonts();
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
