/* Audio: decode (ogg/wav/mp3), load a sounds folder, mix + output device. */
#ifndef AMBIENCE_AUDIO_H
#define AMBIENCE_AUDIO_H

#include <SDL2/SDL.h>
#include "app.h"

/* The output device. 0 when audio failed to open; SDL_LockAudioDevice(0) is a
 * safe no-op, so callers may lock/unlock unconditionally. */
extern SDL_AudioDeviceID g_dev;

int  audio_open(App *a);                      /* 0 ok, -1 on failure */
void audio_close(void);

/* Progress callback: called before decoding each file.
 * (current_index, total_count, filename) — current is 0-based. */
typedef void (*audio_progress_fn)(int cur, int total, const char *name);

void audio_load_sounds(App *a, const char *dir, audio_progress_fn progress);
void audio_free_sounds(App *a);

#endif /* AMBIENCE_AUDIO_H */
