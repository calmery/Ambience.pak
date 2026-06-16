#include "actions.h"
#include "audio.h"       /* g_dev for locking around state changes */

static void mark_dirty(App *a) { a->dirty = 1; a->last_change = SDL_GetTicks(); }

static void change_sel(App *a, float delta)
{
    if (a->nch == 0) return;
    Channel *ch = &a->ch[a->sel];
    SDL_LockAudioDevice(g_dev);
    float base = ch->muted ? ch->saved : ch->target;   /* resume from level */
    float v = base + delta;
    if (v < 0) v = 0; if (v > 1) v = 1;
    ch->target = v;
    ch->muted = 0;                          /* adjusting unmutes */
    if (v > 0) ch->saved = v;
    SDL_UnlockAudioDevice(g_dev);
    mark_dirty(a);
}

static void toggle_mute(App *a)
{
    if (a->nch == 0) return;
    Channel *ch = &a->ch[a->sel];
    SDL_LockAudioDevice(g_dev);
    if (!ch->muted) { ch->muted = 1; ch->saved = ch->target; ch->target = 0; }
    else            { ch->muted = 0; ch->target = ch->saved; }
    SDL_UnlockAudioDevice(g_dev);
    mark_dirty(a);
}

static void toggle_pause(App *a)
{
    SDL_LockAudioDevice(g_dev);
    a->paused = !a->paused;
    a->fade_target = a->paused ? 0.0f : 1.0f;
    a->fade_cur = a->fade_target;           /* pause/resume is instant */
    SDL_UnlockAudioDevice(g_dev);
}

static void cycle_sleep(App *a)
{
    const int STEP = 5, MAXM = 60;          /* minutes */
    SDL_LockAudioDevice(g_dev);
    if (a->sleep_left < 0) {
        a->sleep_left = STEP * 60;
    } else {
        int mins = (int)(a->sleep_left / 60 + 0.5f);
        mins = ((mins + STEP / 2) / STEP) * STEP;
        mins += STEP;
        a->sleep_left = (mins > MAXM) ? -1 : mins * 60;
    }
    SDL_UnlockAudioDevice(g_dev);
}

void act_apply(App *a, int code, int *confirm)
{
    switch (code) {
    case ACT_UP:    if (a->nch) a->sel = (a->sel + a->nch - 1) % a->nch; break;
    case ACT_DOWN:  if (a->nch) a->sel = (a->sel + 1) % a->nch; break;
    case ACT_DEC:   change_sel(a, -0.05f); break;
    case ACT_INC:   change_sel(a, +0.05f); break;
    case ACT_PAUSE: toggle_pause(a); break;
    case ACT_SLEEP: cycle_sleep(a); break;
    case ACT_QUIT:  *confirm = 1; break;
    case ACT_MUTE:  toggle_mute(a); break;
    }
}
