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
void audio_load_sounds(App *a, const char *dir);
void audio_free_sounds(App *a);

#endif /* AMBIENCE_AUDIO_H */
