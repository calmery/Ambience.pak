#include "audio.h"
#include "ui.h"          /* ui_make_disp for channel labels */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern int stb_vorbis_decode_filename(const char *filename, int *channels,
                                      int *sample_rate, short **output);
#include "dr_mp3.h"      /* declarations only; implementation in dr_mp3.c */

SDL_AudioDeviceID g_dev;

/* Decode an .ogg/.wav/.mp3 file into interleaved stereo S16 at SR. */
static int decode_to_pcm(const char *path, short **out, int *frames)
{
    *out = NULL; *frames = 0;
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    SDL_AudioFormat ifmt; int ich, irate;
    Uint8 *src = NULL; Uint32 slen = 0;
    short *vorbis = NULL; Uint8 *wav = NULL; short *mp3buf = NULL;

    if (strcasecmp(ext, ".ogg") == 0) {
        int ch, rate;
        int n = stb_vorbis_decode_filename(path, &ch, &rate, &vorbis);
        if (n <= 0 || !vorbis) return 0;
        ifmt = AUDIO_S16SYS; ich = ch; irate = rate;
        src = (Uint8 *)vorbis; slen = (Uint32)n * ch * 2;
    } else if (strcasecmp(ext, ".wav") == 0) {
        SDL_AudioSpec spec;
        if (!SDL_LoadWAV(path, &spec, &wav, &slen)) return 0;
        ifmt = spec.format; ich = spec.channels; irate = spec.freq;
        src = wav;
    } else if (strcasecmp(ext, ".mp3") == 0) {
        drmp3_config cfg; drmp3_uint64 total = 0;
        mp3buf = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &total, NULL);
        if (!mp3buf || total == 0) { if (mp3buf) drmp3_free(mp3buf, NULL); return 0; }
        ifmt = AUDIO_S16SYS; ich = (int)cfg.channels; irate = (int)cfg.sampleRate;
        src = (Uint8 *)mp3buf; slen = (Uint32)(total * cfg.channels * 2);
    } else {
        return 0;
    }

    int ok = 0;
    SDL_AudioStream *st = SDL_NewAudioStream(ifmt, ich, irate,
                                             AUDIO_S16SYS, 2, SR);
    if (st && SDL_AudioStreamPut(st, src, slen) == 0 &&
        SDL_AudioStreamFlush(st) == 0) {
        int avail = SDL_AudioStreamAvailable(st);
        if (avail > 0) {
            short *dst = malloc(avail);
            int got = dst ? SDL_AudioStreamGet(st, dst, avail) : -1;
            if (got > 0) { *out = dst; *frames = got / 4; ok = 1; }
            else free(dst);
        }
    }
    if (st) SDL_FreeAudioStream(st);
    if (vorbis) free(vorbis);
    if (wav) SDL_FreeWAV(wav);
    if (mp3buf) drmp3_free(mp3buf, NULL);
    return ok;
}

static int cmp_str(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

void audio_load_sounds(App *a, const char *dir)
{
    a->nch = 0;
    DIR *d = opendir(dir);
    if (!d) { SDL_Log("no sounds dir: %s", dir); return; }

    char *names[MAXCH * 2];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAXCH * 2) {
        const char *ext = strrchr(e->d_name, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".ogg") && strcasecmp(ext, ".wav") &&
            strcasecmp(ext, ".mp3")) continue;
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof(char *), cmp_str);

    for (int i = 0; i < n; i++) {
        if (a->nch < MAXCH) {
            char path[600];
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            short *pcm; int fr;
            if (decode_to_pcm(path, &pcm, &fr)) {
                Channel *c = &a->ch[a->nch++];
                memset(c, 0, sizeof *c);
                strncpy(c->name, names[i], sizeof c->name - 1);
                char *dot = strrchr(c->name, '.');
                if (dot) *dot = 0;             /* drop extension */
                ui_make_disp(c->name, c->disp, sizeof c->disp);
                c->data = pcm; c->frames = fr;
                c->target = 0.5f; c->saved = 0.5f;
                SDL_Log("loaded '%s' (%d frames)", c->name, fr);
            } else {
                SDL_Log("failed to decode %s", names[i]);
            }
        }
        free(names[i]);
    }
}

void audio_free_sounds(App *a)
{
    for (int c = 0; c < a->nch; c++) {
        free(a->ch[c].data);
        a->ch[c].data = NULL;
    }
    a->nch = 0;
}

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    App *a = (App *)userdata;
    int16_t *out = (int16_t *)stream;
    int frames = len / (int)(2 * sizeof(int16_t));
    const float vstep = 1.0f / (SR * 0.20f);   /* 200 ms volume slew */
    const float fstep = 1.0f / (SR * 0.50f);   /* 500 ms sleep fade  */

    for (int i = 0; i < frames; i++) {
        if (a->fade_cur < a->fade_target) {
            a->fade_cur += fstep; if (a->fade_cur > a->fade_target) a->fade_cur = a->fade_target;
        } else if (a->fade_cur > a->fade_target) {
            a->fade_cur -= fstep; if (a->fade_cur < a->fade_target) a->fade_cur = a->fade_target;
        }

        float mixL = 0.0f, mixR = 0.0f;
        for (int c = 0; c < a->nch; c++) {
            Channel *ch = &a->ch[c];
            if (ch->cur < ch->target) {
                ch->cur += vstep; if (ch->cur > ch->target) ch->cur = ch->target;
            } else if (ch->cur > ch->target) {
                ch->cur -= vstep; if (ch->cur < ch->target) ch->cur = ch->target;
            }
            if (ch->frames <= 0) continue;
            int p = ch->pos;
            if (ch->cur > 0.0001f) {
                mixL += ch->data[2 * p]     * (1.0f / 32768.0f) * ch->cur;
                mixR += ch->data[2 * p + 1] * (1.0f / 32768.0f) * ch->cur;
            }
            ch->pos = (p + 1 >= ch->frames) ? 0 : p + 1;
        }

        float gain = a->master * a->fade_cur;
        float dL = mixL * gain, dR = mixR * gain;
        float pk = fabsf(dL) > fabsf(dR) ? fabsf(dL) : fabsf(dR);
        if (pk > a->prepeak) a->prepeak = pk;
        out[2 * i]     = (int16_t)(tanhf(dL) * 32767.0f);   /* soft limiter */
        out[2 * i + 1] = (int16_t)(tanhf(dR) * 32767.0f);
        a->frames++;
    }
}

int audio_open(App *a)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    want.userdata = a;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { SDL_Log("OpenAudioDevice failed: %s", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void audio_close(void)
{
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
}
