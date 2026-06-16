# Ambience

An ambient-sound mixer for NextUI handhelds (TrimUI Brick / Smart Pro).
Turn the device into a bedside sound machine: layer your own looping sounds —
rain, ocean, fire, anything — each with its own volume, and fade out on a timer.

## Sounds (bring your own)
Drop audio files into **`res/sounds/`** inside the pak. Each file becomes a
channel named after the file:

- `Fire.ogg` → a **Fire** channel, `Ocean.mp3` → **Ocean**, etc.
- Supported formats: **OGG, WAV, MP3** (any sample rate / mono or stereo — they
  are resampled to 44.1 kHz stereo on load).
- Files loop seamlessly; for best results use steady, loop-friendly recordings.
- No files yet? The app shows a hint and waits — add some and relaunch.

There are no built-in sounds.

## Look & saving
The UI follows the NextUI style — rounded pills, the Rounded M+ font, the
Primary Accent Color (read from NextUI's settings) for the volume bars, and a
button-hint bar. The current mix (each channel's volume + mute state) is written
to `ambience.cfg` next to the binary and restored automatically on the next launch.

## Controls

| Action | Keyboard | TrimUI |
|---|---|---|
| Select channel | Up / Down | D-Pad U/D |
| Adjust volume (hold to repeat) | Left / Right | D-Pad L/R |
| Mute / unmute channel | M / Enter | A |
| Play / Pause | P / Space | X |
| Sleep timer (5-min steps, off..60m) | T | Select |
| Quit (with confirmation) | Q / Esc | B |

Overall loudness is the device's hardware volume buttons.

> TrimUI uses a Nintendo button layout; SDL names buttons by Xbox position, so
> the code maps physical A→SDL_B, physical X→SDL_Y, etc.

## Build & run on desktop (development)

Requires SDL2 + SDL2_ttf (`brew install sdl2 sdl2_ttf`).

```sh
make run         # build and open the window (reads ./res/sounds)
make selftest    # headless: scan res/sounds and report the channels found
```

## Build for NextUI (TrimUI Brick / Smart Pro)

`./build-pak.sh tg5040` cross-compiles with the official NextUI toolchain
(Docker image `ghcr.io/loveretro/tg5040-toolchain`) and writes
`bin/tg5040/ambience`. Then the folder is zipped with `launch.sh`, `pak.json`,
`res/` (font + sounds), and `bin/`.

Install: copy the resulting folder to `/Tools/<platform>/Ambience.pak` on the SD
card. Add or replace audio in `Ambience.pak/res/sounds/` any time.

## Layout (source modules)
- `src/app.h` — shared `Channel` / `App` types + constants
- `src/system.{c,h}` — NextUI integration: accent colour, battery
- `src/audio.{c,h}` — decode (ogg/wav/mp3), resample, load folder, mix, device
- `src/ui.{c,h}` — rendering: fonts, primitives, scroll arrows, screens
- `src/config.{c,h}` — save/load the mix (future home of presets)
- `src/actions.{c,h}` — user actions on the app state
- `src/main.c` — startup, main loop, input mapping, `--selftest` / `--shot`
- `src/stb_vorbis.c`, `src/dr_mp3.c` — bundled OGG / MP3 decoders (static)
- `res/font.ttf` — Rounded M+ 1c Bold (the NextUI rounded font), bundled
- `res/sounds/` — your audio files
- `launch.sh`, `pak.json`, `build-pak.sh` — NextUI pak packaging / build

Dev helpers: `./ambience --selftest` (scan + decode, no audio device) and
`./ambience --shot out.bmp` (render one UI frame offscreen).

## Credits & licenses
Bundled third-party assets/libraries, all redistributable:
- **Font** `res/font.ttf` — Rounded M+ 1c Bold, © 2016 The Rounded M+ Project
  Authors, **SIL Open Font License 1.1** (full text in `res/font.LICENSE.txt`).
- **stb_vorbis** (OGG decoder) — Sean Barrett, public domain / MIT.
- **dr_mp3** (MP3 decoder) — David Reid, public domain (MIT-0).

Audio files are **not** included — add your own to `res/sounds/`.
