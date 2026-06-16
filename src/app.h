/*
 * Ambience - shared types and constants.
 *
 * An ambient-sound mixer for NextUI handhelds. Audio files dropped into
 * res/sounds/ each become a looping channel with its own volume and mute.
 *
 * Module layout:
 *   app.h        - this file: Channel/App types + constants
 *   system.{c,h} - NextUI integration (accent colour, battery)
 *   audio.{c,h}  - decode / load / mix / device (owns g_dev)
 *   ui.{c,h}     - rendering (fonts, shapes, scroll arrows, screens)
 *   config.{c,h} - persistence of the mix (future home of presets)
 *   actions.{c,h}- user actions on the App state
 *   main.c       - startup, main loop, input mapping
 *
 * Future features this layout anticipates:
 *   - Preset tabs: a Preset[] in App + config.c growing to save/load several
 *     named mixes; ui.c gains a tab strip above the channel list.
 *   - Online download: a download.{c,h} that fetches files into res/sounds/
 *     then calls audio_load_sounds() to rescan.
 */
#ifndef AMBIENCE_APP_H
#define AMBIENCE_APP_H

#include <SDL2/SDL.h>

#define SR        44100  /* mixing/output sample rate */
#define UI_W      1024   /* logical screen size (TrimUI Brick) */
#define UI_H      768
#define MAXCH     32     /* max simultaneous sound channels */
#define MAXPRESET 8      /* max saved presets (tabs, future UI) */

typedef struct {
    char name[48];   /* full name, used as the config key       */
    char disp[40];   /* truncated label for on-screen display   */
    short *data;     /* decoded interleaved stereo S16 at SR    */
    int frames;      /* stereo frames in data                   */
    int pos;         /* current play head (frame)               */
    float target;    /* desired volume 0..1 (UI thread)         */
    float cur;       /* smoothed volume   (audio thread)        */
    float saved;     /* level remembered across mute            */
    int muted;
} Channel;

/* A preset stores a mix by channel name, so it survives files being
 * added/removed. Entries that don't match a loaded channel are kept and
 * re-saved untouched. */
typedef struct {
    char name[48];   /* channel name (matches Channel.name) */
    float level;     /* volume 0..1                          */
    int muted;
} PresetEntry;

typedef struct {
    char name[32];
    PresetEntry e[MAXCH];
    int ne;
} Preset;

typedef struct {
    Channel ch[MAXCH];
    int nch;
    float master, fade_cur, fade_target;
    int paused, sel, scroll;
    float sleep_left;           /* seconds remaining, < 0 = off */
    int dirty;                  /* mix changed, needs saving    */
    Uint32 last_change;
    /* presets: only the active one is editable today; a future tab strip in
     * ui.c will switch `active` and call config_apply_active(). */
    Preset presets[MAXPRESET];
    int npreset;
    int active;
    double prepeak;             /* diagnostics (audio peak)     */
    long frames;
} App;

#endif /* AMBIENCE_APP_H */
