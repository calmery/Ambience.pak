#include "config.h"
#include <stdio.h>
#include <string.h>

#define CONFIG_PATH "ambience.cfg"

static void clampf(float *v) { if (*v < 0) *v = 0; if (*v > 1) *v = 1; }

void config_load(App *a)
{
    a->npreset = 0;
    a->active = 0;
    int cur = -1;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[160];
        while (fgets(line, sizeof line, f)) {
            int idx; char nm[48]; float v; int m;
            if (sscanf(line, "active %d", &idx) == 1) {
                a->active = idx;
            } else if (sscanf(line, "preset %31[^\n]", nm) == 1) {
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
    for (int c = 0; c < a->nch; c++) {
        Channel *ch = &a->ch[c];
        for (int i = 0; i < p->ne; i++) {
            if (strcmp(ch->name, p->e[i].name) != 0) continue;
            ch->saved = p->e[i].level;
            if (p->e[i].muted) { ch->muted = 1; ch->target = 0; }
            else               { ch->muted = 0; ch->target = p->e[i].level; }
            break;
        }
    }
}

void config_save(App *a)
{
    if (a->npreset == 0) return;

    /* capture the live channel mix into the active preset */
    Preset *p = &a->presets[a->active];
    p->ne = 0;
    for (int c = 0; c < a->nch && p->ne < MAXCH; c++) {
        PresetEntry *pe = &p->e[p->ne++];
        strncpy(pe->name, a->ch[c].name, sizeof pe->name - 1);
        pe->name[sizeof pe->name - 1] = 0;
        pe->level = a->ch[c].muted ? a->ch[c].saved : a->ch[c].target;
        pe->muted = a->ch[c].muted;
    }

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
