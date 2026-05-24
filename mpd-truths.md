# Volumio FBD Notes of Truth

Date: 2026-05-24
Checkpoint: after MPD logging build and review of logs after 4:00 PM MST

## Source truth

Volumio is the unified playback source.

MPD did not report Spotify or AirPlay playback as active in the captured log. MPD repeatedly reported:

```text
state=stop
volume=-1
title=''
artist=''
album=''
```

At the same time, Volumio reported active Spotify playback using:

```text
vol_source='spop'
vol_state='play' or 'pause'
active_title='...'
art_url='https://i.scdn.co/image/...'
```

## Spotify truth

Spotify source appears as:

```text
spop
```

Spotify provides album art directly through Volumio.

The observed Spotify art URLs were real Spotify CDN URLs:

```text
https://i.scdn.co/image/...
```

Design rule:

```text
For spop, trust albumart.
Do not use fallback art logic for Spotify.
```

## AirPlay truth

AirPlay appears through Volumio, not MPD.

Observed source name:

```text
airplay_emulation
```

Design rule:

```text
AirPlay must be handled through Volumio getState.
Do not depend on MPD for AirPlay display state.
```

## MPD truth

MPD is not useful for unified playback state in this setup.

It may still be useful only if future local-library testing proves that Volumio lacks something MPD has.

Current design rule:

```text
Remove MPD from the main display path.
Use Volumio getState only.
```

## Code direction

Keep:

```text
Volumio getState
big idle clock
small playback screen
scrolling title
album art
volume display
source labels if useful
```

Cut or avoid:

```text
MPD polling
MPD parser
MPD logging
alpha colon effect
pause timeout
AirPlay stall timer
external stale timer
dirty rectangles
album background fade
Spotify fallback album art
custom socket HTTP
complex threading
```

## Album art rule

```text
Spotify/spop:
  trust albumart

AirPlay/local/radio/plugin:
  use albumart if present
  fallback only if needed
```

## Current best branch

```text
volumio_fbd_lite_spotify_trust_art.c
```

This is the best checkpoint before further miniaturization.
