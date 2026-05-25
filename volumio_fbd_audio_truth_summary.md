# Volumio FBD Audio Truth Summary

Date: 2026-05-24  
Checkpoint: ALSA `hw_params` confirmed as a hardware-level audio format source.

## Current truth stack

The display should use separate truth sources for separate jobs:

```text
ALSA status      = is audio physically playing
ALSA hw_params   = actual output format
Volumio getState = source, metadata, artwork, volume
JSON config      = user-tunable display behavior
```

## ALSA status truth

Playback activity should come from:

```text
/proc/asound/card*/pcm*/sub*/status
```

If any active playback status contains:

```text
state: RUNNING
```

then audio is actually playing.

If no status file shows `RUNNING`, audio is not actively playing.

This should drive the return-to-big-clock timer.

## ALSA hw_params truth

Actual output format should come from:

```text
/proc/asound/card*/pcm*/sub*/hw_params
```

Example observed output:

```text
closed
closed
access: MMAP_INTERLEAVED
format: S32_LE
subformat: STD
channels: 2
rate: 44100 (44100/1)
period_size: 256
buffer_size: 65536
```

This means:

```text
Audio device is open
Output format is S32_LE
Output is stereo
Output rate is 44.1 kHz
```

## Important distinction

Volumio may report the source media as:

```text
16 bit
44.1 KHz
```

But ALSA may report the actual DAC/output format as:

```text
S32_LE
44100
2 channels
```

So:

```text
Volumio = media/source metadata
ALSA hw_params = actual hardware output format
```

## Display mapping

Raw ALSA format should be converted to friendly display text.

Examples:

```text
S16_LE   -> 16-bit
S24_LE   -> 24-bit
S24_3LE  -> 24-bit
S32_LE   -> 32-bit
FLOAT_LE -> Float
```

Channels:

```text
1 -> Mono
2 -> Stereo
6 -> 5.1
8 -> 7.1
```

Rate:

```text
44100  -> 44.1 kHz
48000  -> 48 kHz
96000  -> 96 kHz
192000 -> 192 kHz
```

Example display string:

```text
44.1 kHz / 32-bit / Stereo
```

## Spotify display rule

Spotify should continue to use Volumio for:

```text
title
artist
album
album art
seek
duration
volume
source identity
```

Spotify should use ALSA hw_params for the actual output format line:

```text
44.1 kHz / 32-bit / Stereo
```

## AirPlay display rule

AirPlay should remain a clean source screen.

Show:

```text
AirPlay
Volume %
actual output format if ALSA is running
```

Example:

```text
AirPlay
Volume 63%
44.1 kHz / 32-bit / Stereo
```

Do not show AirPlay title, artist, album, album art, or progress unless future logs prove they are reliable enough.

## Return-to-clock rule

Return-to-clock behavior should use ALSA status, not Volumio source cleanup.

```text
ALSA RUNNING:
  keep source screen active

ALSA not RUNNING:
  start return_to_clock_seconds timer

Timer expires:
  clear media display
  show big clock
```

The delay belongs in JSON:

```json
{
  "display": {
    "return_to_clock_seconds": 5.0
  }
}
```

## What hw_params cannot tell us

ALSA `hw_params` cannot identify:

```text
song title
artist
album
Spotify vs AirPlay
album art
pause vs stop intent
```

It only tells us the actual audio output format while the device is open.

## Current design principle

Use hardware truth where hardware truth exists.

```text
ALSA status answers: is audio moving?
ALSA hw_params answers: what format is the DAC receiving?
Volumio answers: what source/media does the user see?
JSON answers: how should the display behave?
```
