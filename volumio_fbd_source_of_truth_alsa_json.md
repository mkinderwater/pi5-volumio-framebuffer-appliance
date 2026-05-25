# Volumio FBD Source of Truth

Date: 2026-05-24
Checkpoint: ALSA audio activity added as playback truth, with JSON-configurable return-to-clock timer.

## Final architecture truth

The display now has three separate responsibilities:

```text
Volumio = metadata and source identity
ALSA = audio activity truth
JSON = user-tunable display behavior
```

Do not collapse these roles back together.

## Volumio truth

Volumio remains the source for:

```text
status
service
trackType
title
artist
album
albumart
seek
duration
samplerate
bitdepth
volume
```

Endpoint:

```text
http://127.0.0.1:3000/api/v1/getState
```

Volumio is still needed because it understands Spotify, AirPlay, local playback, radio, and plugins better than MPD in this setup.

## MPD truth

MPD is not part of the production path.

Reason:

```text
MPD stayed stopped/empty while Volumio showed Spotify and AirPlay activity.
```

Do not re-add MPD unless new logs prove it adds useful information that Volumio and ALSA do not already provide.

## ALSA truth

ALSA is the truth for whether audio is actually playing.

Test evidence:

```text
/proc/asound/card2/pcm0p/sub0/status:state: RUNNING
```

appeared while audio was active.

When playback stopped, the RUNNING line disappeared.

Production rule:

```text
If any /proc/asound/card*/pcm*/sub*/status contains state: RUNNING,
audio is active.
```

## Display state truth

The display should not depend on Spotify pause, AirPlay pause, or Volumio cleanup behavior to return to the big clock.

Universal rule:

```text
ALSA RUNNING keeps the source screen alive.
ALSA not RUNNING starts the return-to-clock timer.
```

## Real idle truth

Real idle is still immediate:

```text
status=stop
service=mpd
title empty
duration=0
```

When real idle is detected:

```text
show big clock immediately
clear artwork
clear metadata
```

## Return-to-clock truth

The audio stop behavior is now:

```text
audio stopped -> start return_to_clock_seconds timer -> big clock
```

This is configurable in JSON.

Config path:

```text
/etc/volumio_fbd_config.json
```

Default config:

```json
{
  "display": {
    "return_to_clock_seconds": 5.0
  }
}
```

Rule:

```text
Do not hard-code the return-to-clock delay.
Use the JSON value.
```

## Spotify truth

Spotify appears as:

```text
service=spop
trackType=spotify
```

Spotify is reliable enough for full display.

Show:

```text
album art
title
artist
album
progress
volume
pause/play state if useful
scrolling title
```

Spotify album art is direct and trusted:

```text
https://i.scdn.co/image/...
```

Do not use fallback art for Spotify.

## AirPlay truth

AirPlay appears as:

```text
service=airplay_emulation
trackType=airplay
```

AirPlay metadata and album art are inconsistent.

Final display rule:

```text
AirPlay gets a clean source screen.
```

Show only:

```text
AirPlay
Volume
simple centered visual / icon / bar
```

Do not show AirPlay:

```text
title
artist
album
album art
progress
pause state
```

## AirPlay return-to-clock truth

Do not try to detect AirPlay pause.

Do not infer AirPlay quit from metadata.

Use ALSA instead:

```text
If ALSA is RUNNING:
  keep AirPlay screen

If ALSA is not RUNNING:
  start return_to_clock_seconds timer

If timer expires:
  show big clock
```

## Removed assumptions

Do not bring these back without new evidence:

```text
MPD polling
MPD parser
MPD logging
AirPlay pause detection
AirPlay track hold
AirPlay album-art display
AirPlay progress
Spotify fallback artwork
pause timeout
alpha colon blink
dirty rectangles
custom HTTP socket stack
complex worker threads
album background fade
debug logging in production
```

## Kept features

```text
large idle clock
Volumio-only metadata
ALSA audio activity detection
JSON-configurable return-to-clock delay
Spotify full media screen
Spotify scrolling title
Spotify album art
Spotify progress
Spotify volume
AirPlay clean source screen
curl-only HTTP
small production codebase
```

## Current production branch

```text
volumio_fbd_truth_alsa_clock_json.c
```

## Core production logic

```c
bool real_idle =
    strcmp(state, "stop") == 0 &&
    strcmp(source, "mpd") == 0 &&
    title[0] == '\0' &&
    duration <= 0;

bool audio_running = alsa_audio_running();

if (real_idle) {
    clear_artwork();
    show_big_clock();
} else if (audio_running) {
    reset_return_to_clock_timer();
    show_source_screen();
} else {
    start_or_continue_return_to_clock_timer();

    if (return_to_clock_timer_expired()) {
        clear_artwork();
        show_big_clock();
    } else {
        show_source_screen();
    }
}
```

## Design principle

Prefer physical truth over software guesses.

Volumio tells us what source and metadata exists.
ALSA tells us whether audio is actually moving.
JSON lets the user tune the feel without recompiling.
