#include "config.h"
#include "audio.h"       /* g_dev: lock around channel writes */
#include <stdio.h>
#include <string.h>

#define CONFIG_PATH "ambience.cfg"

static void clampf(float *v) { if (*v < 0) *v = 0; if (*v > 1) *v = 1; }

static void touch(App *a) { a->dirty = 1; a->last_change = SDL_GetTicks(); }

void config_load(App *a)
{
    a->npreset = 0;
    a->active = 0;
    int cur = -1;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[160];
        while (fgets(line, sizeof line, f)) {
            int idx; char nm[64]; float v; int m;
            if (sscanf(line, "active %d", &idx) == 1) {
                a->active = idx;
            } else if (sscanf(line, "preset %63[^\n]", nm) == 1) {
                if (a->npreset < MAXPRESET) {
                    cur = a->npreset++;
                    strncpy(a->presets[cur].name, nm, sizeof a->presets[cur].name - 1);
                    a->presets[cur].name[sizeof a->presets[cur].name - 1] = 0;
                    a->presets[cur].ne = 0;
                }
            } else if (sscanf(line, "vol %f %d %47[^\n]", &v, &m, nm) == 3) {
                if (cur >= 0 && a->presets[cur].ne < MAXCH) {
                    PresetEntry *pe = &a->presets[cur].e[a->presets[cur].ne++];
                    clampf(&v);
                    strncpy(pe->name, nm, sizeof pe->name - 1);
                    pe->name[sizeof pe->name - 1] = 0;
                    pe->level = v;
                    pe->muted = m ? 1 : 0;
                }
            }
        }
        fclose(f);
    }

    if (a->npreset == 0) {                 /* first run: a single empty preset */
        strncpy(a->presets[0].name, "Default", sizeof a->presets[0].name - 1);
        a->presets[0].ne = 0;
        a->npreset = 1;
    }
    if (a->active < 0 || a->active >= a->npreset) a->active = 0;
}

void config_apply_active(App *a)
{
    Preset *p = &a->presets[a->active];
    SDL_LockAudioDevice(g_dev);
    for (int c = 0; c < a->nch; c++) {
        Channel *ch = &a->ch[c];
        ch->target = 0; ch->saved = 0; ch->muted = 0;   /* default if unlisted */
        for (int i = 0; i < p->ne; i++) {
            if (strcmp(ch->name, p->e[i].name) != 0) continue;
            ch->saved = p->e[i].level;
            if (p->e[i].muted) { ch->muted = 1; ch->target = 0; }
            else               { ch->muted = 0; ch->target = p->e[i].level; }
            break;
        }
    }
    SDL_UnlockAudioDevice(g_dev);
}

void config_capture_active(App *a)
{
    if (a->npreset == 0) return;
    Preset *p = &a->presets[a->active];
    PresetEntry out[MAXCH];
    int ne = 0;

    /* capture the current mix from every loaded channel */
    for (int c = 0; c < a->nch && ne < MAXCH; c++) {
        PresetEntry *pe = &out[ne++];
        strncpy(pe->name, a->ch[c].name, sizeof pe->name - 1);
        pe->name[sizeof pe->name - 1] = 0;
        pe->level = a->ch[c].muted ? a->ch[c].saved : a->ch[c].target;
        pe->muted = a->ch[c].muted;
    }
    /* preserve entries whose file isn't currently loaded, so a temporarily
     * removed sound keeps its saved level (matches the Preset doc in app.h) */
    for (int i = 0; i < p->ne && ne < MAXCH; i++) {
        int loaded = 0;
        for (int c = 0; c < a->nch; c++)
            if (!strcmp(p->e[i].name, a->ch[c].name)) { loaded = 1; break; }
        if (!loaded) out[ne++] = p->e[i];
    }
    memcpy(p->e, out, (size_t)ne * sizeof *out);
    p->ne = ne;
}

void config_save(App *a)
{
    config_capture_active(a);
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "active %d\n", a->active);
    for (int i = 0; i < a->npreset; i++) {
        fprintf(f, "preset %s\n", a->presets[i].name);
        for (int e = 0; e < a->presets[i].ne; e++)
            fprintf(f, "vol %.3f %d %s\n", a->presets[i].e[e].level,
                    a->presets[i].e[e].muted, a->presets[i].e[e].name);
    }
    fclose(f);
    a->dirty = 0;
}

void config_switch(App *a, int idx)
{
    if (idx < 0 || idx >= a->npreset || idx == a->active) return;
    config_capture_active(a);          /* keep edits in the preset we leave */
    a->active = idx;
    config_apply_active(a);
    touch(a);
}

int config_new(App *a, const char *name)
{
    if (a->npreset >= MAXPRESET) return -1;
    config_capture_active(a);          /* persist current edits first */
    int idx = a->npreset++;
    strncpy(a->presets[idx].name, name, sizeof a->presets[idx].name - 1);
    a->presets[idx].name[sizeof a->presets[idx].name - 1] = 0;
    a->presets[idx].ne = 0;
    a->active = idx;
    config_capture_active(a);          /* seed the new preset from current mix */
    touch(a);
    return idx;
}

void config_rename(App *a, int idx, const char *name)
{
    if (idx < 0 || idx >= a->npreset || !name[0]) return;
    strncpy(a->presets[idx].name, name, sizeof a->presets[idx].name - 1);
    a->presets[idx].name[sizeof a->presets[idx].name - 1] = 0;
    touch(a);
}

void config_delete(App *a, int idx)
{
    if (a->npreset <= 1 || idx < 0 || idx >= a->npreset) return;
    for (int i = idx; i < a->npreset - 1; i++) a->presets[i] = a->presets[i + 1];
    a->npreset--;
    if (a->active >= a->npreset) a->active = a->npreset - 1;
    config_apply_active(a);
    touch(a);
}
