# Volumio FBD Notes of Truth - Volumio State Machine

Date: 2026-05-24
Evidence: volumio-test-log.txt
Purpose: preserve the known-good source and state rules before further code reduction.

## Primary truth

Volumio getState is the unified source of truth.

Do not use MPD for unified playback display.

MPD did not reliably expose Spotify or AirPlay playback in this setup. Volumio does.

## Real idle truth

Only treat the system as idle when Volumio reports all of this:

```text
state=stop
source=mpd
title empty
duration=0
```

Only this condition should return the display to the big clock and clear metadata/artwork.

## Spotify truth

Spotify appears as:

```text
source=spop
type=spotify
```

Spotify provides:

```text
state
title
artist
album
seek
duration
volume
samplerate
bitdepth
albumart
```

Spotify album art is reliable in the tested log.

Observed pattern:

```text
albumart=https://i.scdn.co/image/...
```

Rule:

```text
Trust Spotify albumart.
Do not use fallback art for Spotify.
```

## Spotify stop truth

Spotify can briefly report:

```text
state=stop
source=spop
title present
duration > 0
albumart present
```

This is not true idle.

Rule:

```text
Do not clear artwork on spop + stop.
Do not return to big clock on spop + stop.
Treat it as a hold/pause-like transition state.
Wait for source=mpd + state=stop + empty title + duration=0.
```

## Spotify pause truth

Spotify pause reports:

```text
state=pause
source=spop
seek frozen
metadata present
artwork present
```

Rule:

```text
Keep playback screen visible.
Do not use pause timeout.
Do not clear artwork.
```

## AirPlay truth

AirPlay appears as:

```text
source=airplay_emulation
type=airplay
```

AirPlay can provide full metadata and artwork:

```text
title present
artist present
album present
duration > 0
albumart=/albumart?cacheid=...
```

AirPlay can also be active with weak metadata:

```text
state=play
source=airplay_emulation
title empty
duration=0
albumart=/albumart
```

Rule:

```text
Treat airplay_emulation as active even if title/duration are blank.
Do not force big clock just because AirPlay metadata is empty.
```

## AirPlay artwork truth

AirPlay artwork should be used when Volumio provides it.

Preferred:

```text
/albumart?cacheid=...
```

Fallback:

```text
/albumart
```

Rule:

```text
AirPlay may use Volumio local albumart.
Do not apply Spotify assumptions to AirPlay.
```

## Timeout truth

Remove pause timeout.

Do not return to big clock because something is paused.

Do not use an AirPlay stall timer unless later evidence proves Volumio never reaches real idle.

Rule:

```text
Big clock is state-driven, not timeout-driven.
```

## Artwork clearing truth

Only clear artwork on real idle:

```text
state=stop
source=mpd
title empty
duration=0
```

Do not clear artwork on:

```text
spop + stop
spop + pause
airplay_emulation + play
airplay_emulation + blank title
```

## Best simplified active logic

```c
bool is_real_idle =
    strcmp(state, "stop") == 0 &&
    strcmp(source, "mpd") == 0 &&
    title[0] == '\0' &&
    duration <= 0;

bool is_spotify =
    strcmp(source, "spop") == 0 ||
    strcmp(track_type, "spotify") == 0;

bool is_airplay =
    strcmp(source, "airplay_emulation") == 0 ||
    strcmp(track_type, "airplay") == 0;

bool is_active =
    strcmp(state, "play") == 0 ||
    strcmp(state, "pause") == 0 ||
    is_spotify ||
    is_airplay;

if (is_real_idle) {
    clear_artwork();
    show_big_clock();
} else if (is_active) {
    show_playback_screen();
} else {
    show_big_clock();
}
```

## Current production direction

Start from:

```text
volumio_fbd_lite_spotify_trust_art.c
```

Apply:

```text
Volumio-only
no MPD
no alpha effect
no pause timeout
no AirPlay timeout
no Spotify fallback art
no artwork clearing unless real idle
AirPlay always active when source is airplay_emulation
Spotify held active through spop stop transitions
```

## Keep

```text
big clock
small playback layout
scrolling title
album art
volume display
Volumio getState
```

## Cut

```text
MPD
MPD logging
pause timeout
AirPlay stall timer
Spotify fallback art
complex threading
custom HTTP
dirty rectangles
alpha colon effect
album background fade
debug logging in production
```
