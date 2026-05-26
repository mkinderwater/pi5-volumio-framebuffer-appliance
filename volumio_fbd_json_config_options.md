# Volumio FBD JSON Configuration Reference

Build target: `volumio_fbd_truth_threaded_poll.c`  
Config path: `/etc/volumio_fbd_config.json`

## Purpose

The JSON file controls display behavior and visual tuning without recompiling the binary.

It should control:

```text
framebuffer path
render dimensions
clock format
return-to-clock timing
visual effects
layout sizes
font sizes
colors
```

It should **not** control playback truth.

Playback truth remains:

```text
Volumio = metadata/source/artwork/volume
ALSA status = audio running truth
ALSA hw_params = actual output format
JSON = user-tunable display behavior
```

## Full example

```json
{
  "display": {
    "fb_path": "/dev/fb0",
    "width": 480,
    "height": 320,
    "clock_type": "12h",
    "return_to_clock_seconds": 5.0
  },
  "visual": {
    "colon_alpha_fade_enabled": true,
    "colon_alpha_fade_hz": 0.5,
    "colon_alpha_min": 32,
    "colon_alpha_max": 255,
    "album_background_enabled": true,
    "album_background_zoom_percent": 220,
    "album_background_brightness_percent": 42,
    "album_background_diagonal_fade_when_title_scrolls": true,
    "album_background_diagonal_fade_start": 96,
    "album_background_diagonal_fade_end": 128
  },
  "ui": {
    "padding": 24,
    "footer_height": 24,
    "header_height": 32,
    "progress_bar_height": 7,
    "volume_bar_width": 20,
    "volume_bar_height": 228,
    "volume_bar_right_margin": 32,
    "volume_overlay_seconds": 2,
    "album_art_size": 150,
    "generic_album_art_size": 136,
    "title_font_size": 26,
    "artist_font_size": 18,
    "album_font_size": 15,
    "source_font_size": 14,
    "footer_font_size": 11,
    "small_clock_font_size": 16,
    "idle_clock_font_size": 132,
    "idle_date_font_size": 20,
    "idle_ip_font_size": 16,
    "airplay_icon_width": 132,
    "airplay_icon_height": 78
  },
  "colors": {
    "background": "#000000",
    "text_main": "#FFFFFF",
    "text_dim": "#AAAAAA",
    "text_muted": "#777777",
    "panel": "#050505",
    "panel_line": "#242424",
    "progress_bg": "#202020",
    "progress_fg": "#00FF00",
    "volume_bar": "#FFFFFF",
    "album_placeholder": "#181818"
  }
}
```

## Config loading behavior

On startup, the program reads:

```text
/etc/volumio_fbd_config.json
```

If the file does not exist, the program creates a default file.

If the file exists, the program reads values from it and falls back to built-in defaults for missing fields.

The program does not overwrite an existing config file.

## Section: display

Controls framebuffer and high-level display behavior.

### `fb_path`

Default:

```json
"fb_path": "/dev/fb0"
```

Type:

```text
string
```

Purpose:

```text
Framebuffer device path.
```

Typical values:

```text
/dev/fb0
/dev/fb1
```

Use this if the LCD is not on `/dev/fb0`.

### `width`

Default:

```json
"width": 480
```

Type:

```text
integer
```

Accepted range:

```text
1 to 8192
```

Purpose:

```text
Render width in pixels.
```

Notes:

```text
This controls the program's render surface.
The value is clamped to a safe range.
Use the actual LCD width unless intentionally rendering smaller.
```

### `height`

Default:

```json
"height": 320
```

Type:

```text
integer
```

Accepted range:

```text
1 to 8192
```

Purpose:

```text
Render height in pixels.
```

Notes:

```text
Use the actual LCD height unless intentionally rendering smaller.
```

### `clock_type`

Default:

```json
"clock_type": "12h"
```

Type:

```text
string
```

Accepted values:

```text
12h
12
12-hour
24h
24
24-hour
```

Purpose:

```text
Controls the idle clock format.
```

Examples:

```json
"clock_type": "12h"
```

```json
"clock_type": "24h"
```

### `return_to_clock_seconds`

Default:

```json
"return_to_clock_seconds": 5.0
```

Type:

```text
number
```

Accepted range:

```text
0.0 to 3600.0
```

Purpose:

```text
Controls how long the display waits after ALSA audio stops before returning to the big clock.
```

Behavior:

```text
ALSA RUNNING:
  keep the source screen active

ALSA not RUNNING:
  start this timer

Timer expires:
  clear media display
  show big clock
```

Recommended values:

```text
3.0  = quick clock return
5.0  = default
8.0  = more forgiving during source handoff
15.0 = slow return
```

Use `0.0` only if you want instant return behavior.

## Section: visual

Controls visual effects.

### `colon_alpha_fade_enabled`

Default:

```json
"colon_alpha_fade_enabled": true
```

Type:

```text
boolean
```

Purpose:

```text
Enables the signature fading colon effect on the clock.
```

Notes:

```text
This is visual only.
It does not affect timing, audio truth, or playback logic.
```

### `colon_alpha_fade_hz`

Default:

```json
"colon_alpha_fade_hz": 0.5
```

Type:

```text
number
```

Accepted range:

```text
0.1 to 10.0
```

Purpose:

```text
Controls clock-colon fade speed.
```

Meaning:

```text
0.5 Hz = one full fade cycle every 2 seconds
1.0 Hz = one full fade cycle every 1 second
```

Recommended for low-budget LCDs:

```json
"colon_alpha_fade_hz": 0.5
```

### `colon_alpha_min`

Default:

```json
"colon_alpha_min": 32
```

Type:

```text
integer
```

Accepted range:

```text
0 to 255
```

Purpose:

```text
Lowest alpha value used during colon fade.
```

Recommended:

```text
0   = colon fully disappears
32  = subtle minimum glow
128 = never gets very dim
```

### `colon_alpha_max`

Default:

```json
"colon_alpha_max": 255
```

Type:

```text
integer
```

Accepted range:

```text
0 to 255
```

Purpose:

```text
Highest alpha value used during colon fade.
```

Notes:

```text
If min is greater than max, the program swaps them internally.
```

### `album_background_enabled`

Default:

```json
"album_background_enabled": true
```

Type:

```text
boolean
```

Purpose:

```text
Enables Spotify/trusted album-art background.
```

Important rule:

```text
Spotify may use album background.
AirPlay must not use album background.
Idle clock must not use album background.
```

### `album_background_zoom_percent`

Default:

```json
"album_background_zoom_percent": 220
```

Type:

```text
integer
```

Accepted range:

```text
100 to 600
```

Purpose:

```text
Controls zoom level for the background album art.
```

Meaning:

```text
100 = no extra zoom
220 = default strong crop/zoom
300 = more abstract background
```

### `album_background_brightness_percent`

Default:

```json
"album_background_brightness_percent": 42
```

Type:

```text
integer
```

Accepted range:

```text
0 to 100
```

Purpose:

```text
Controls background brightness after dimming.
```

Recommended values:

```text
25 = very dark
42 = default
60 = brighter, may reduce readability
```

### `album_background_diagonal_fade_when_title_scrolls`

Default:

```json
"album_background_diagonal_fade_when_title_scrolls": true
```

Type:

```text
boolean
```

Purpose:

```text
Applies the diagonal fade only when the Spotify title is scrolling.
```

Behavior:

```text
Short title:
  full dimmed zoomed background

Long scrolling title:
  dimmed zoomed background fades to black by the configured diagonal endpoint
```

### `album_background_diagonal_fade_start`

Default:

```json
"album_background_diagonal_fade_start": 96
```

Type:

```text
integer
```

Accepted range:

```text
0 to 255
```

Purpose:

```text
Controls where the diagonal fade begins.
```

Scale:

```text
0   = lower-left side
128 = 50% diagonal point
255 = upper-right side
```

### `album_background_diagonal_fade_end`

Default:

```json
"album_background_diagonal_fade_end": 128
```

Type:

```text
integer
```

Accepted range:

```text
0 to 255
```

Purpose:

```text
Controls where the diagonal fade reaches black.
```

Default rule:

```text
Fade reaches black at 128, the 50% diagonal.
```

Notes:

```text
If fade_start is greater than or equal to fade_end, the program resets them to 96 and 128.
```

## Section: ui

Controls spacing, sizes, and typography.

All values are integers.

### `padding`

Default:

```json
"padding": 24
```

Accepted range:

```text
0 to 80
```

Purpose:

```text
General screen padding.
```

Affects:

```text
Spotify layout
text bounds
album art placement
progress area
safe edge spacing
```

### `footer_height`

Default:

```json
"footer_height": 24
```

Accepted range:

```text
0 to 80
```

Purpose:

```text
Reserved footer area height.
```

Notes:

```text
The current LCD direction avoids clutter.
This is mostly useful for compact source/format lines where enabled.
```

### `header_height`

Default:

```json
"header_height": 32
```

Accepted range:

```text
0 to 80
```

Purpose:

```text
Reserved top/header spacing.
```

### `progress_bar_height`

Default:

```json
"progress_bar_height": 7
```

Accepted range:

```text
1 to 40
```

Purpose:

```text
Spotify progress bar height.
```

Recommended:

```text
5 to 8 for small LCDs
10 to 14 for larger panels
```

### `volume_bar_width`

Default:

```json
"volume_bar_width": 20
```

Accepted range:

```text
4 to 80
```

Purpose:

```text
Width of the right-side vertical volume bar.
```

Current design truth:

```text
Volume bar is vertical, right-side, and white by default.
```

### `volume_bar_height`

Default:

```json
"volume_bar_height": 228
```

Accepted range:

```text
40 to 400
```

Purpose:

```text
Height of the right-side vertical volume bar.
```

### `volume_bar_right_margin`

Default:

```json
"volume_bar_right_margin": 32
```

Accepted range:

```text
0 to 120
```

Purpose:

```text
Distance from right edge to the vertical volume bar.
```

Higher value moves the bar left.

### `volume_overlay_seconds`

Default:

```json
"volume_overlay_seconds": 2
```

Accepted range:

```text
1 to 20
```

Purpose:

```text
How long the volume bar remains visible after volume changes.
```

### `album_art_size`

Default:

```json
"album_art_size": 150
```

Accepted range:

```text
48 to 260
```

Purpose:

```text
Spotify album art tile size.
```

### `generic_album_art_size`

Default:

```json
"generic_album_art_size": 136
```

Accepted range:

```text
48 to 260
```

Purpose:

```text
Generic rich-source album art size, if used.
```

Notes:

```text
AirPlay does not use album art.
Spotify uses album_art_size.
```

### `title_font_size`

Default:

```json
"title_font_size": 26
```

Accepted range:

```text
10 to 64
```

Purpose:

```text
Spotify title font size.
```

### `artist_font_size`

Default:

```json
"artist_font_size": 18
```

Accepted range:

```text
8 to 48
```

Purpose:

```text
Spotify artist font size.
```

### `album_font_size`

Default:

```json
"album_font_size": 15
```

Accepted range:

```text
8 to 48
```

Purpose:

```text
Spotify album font size.
```

### `source_font_size`

Default:

```json
"source_font_size": 14
```

Accepted range:

```text
8 to 36
```

Purpose:

```text
Source label font size, where source labels are used.
```

### `footer_font_size`

Default:

```json
"footer_font_size": 11
```

Accepted range:

```text
8 to 32
```

Purpose:

```text
Footer/format line font size.
```

### `small_clock_font_size`

Default:

```json
"small_clock_font_size": 16
```

Accepted range:

```text
8 to 48
```

Purpose:

```text
Small clock text size on media screens.
```

### `idle_clock_font_size`

Default:

```json
"idle_clock_font_size": 132
```

Accepted range:

```text
40 to 220
```

Purpose:

```text
Large idle clock size.
```

Idle screen should only show:

```text
clock
date
device IP
```

### `idle_date_font_size`

Default:

```json
"idle_date_font_size": 20
```

Accepted range:

```text
8 to 48
```

Purpose:

```text
Idle date font size.
```

### `idle_ip_font_size`

Default:

```json
"idle_ip_font_size": 16
```

Accepted range:

```text
8 to 40
```

Purpose:

```text
Idle IP address font size.
```

### `airplay_icon_width`

Default:

```json
"airplay_icon_width": 132
```

Accepted range:

```text
48 to 260
```

Purpose:

```text
Width of centered AirPlay-style icon.
```

Current AirPlay display truth:

```text
AirPlay icon only
optional output format
no "AirPlay" word
no title
no artist
no album
no album art
no progress
```

### `airplay_icon_height`

Default:

```json
"airplay_icon_height": 78
```

Accepted range:

```text
32 to 180
```

Purpose:

```text
Height of centered AirPlay-style icon.
```

## Section: colors

Colors must be 6-digit RGB hex strings.

Accepted:

```json
"#FFFFFF"
```

Also accepted:

```json
"FFFFFF"
```

Not accepted by the current parser:

```json
"0xFFFFFF"
```

The parser expects 6 hex digits, optionally prefixed with `#`.

### `background`

Default:

```json
"background": "#000000"
```

Purpose:

```text
Main background color.
```

For the truth design, black is recommended.

### `text_main`

Default:

```json
"text_main": "#FFFFFF"
```

Purpose:

```text
Primary text color.
```

### `text_dim`

Default:

```json
"text_dim": "#AAAAAA"
```

Purpose:

```text
Secondary text color.
```

### `text_muted`

Default:

```json
"text_muted": "#777777"
```

Purpose:

```text
Muted text color.
```

### `panel`

Default:

```json
"panel": "#050505"
```

Purpose:

```text
Panel or subtle UI background color.
```

### `panel_line`

Default:

```json
"panel_line": "#242424"
```

Purpose:

```text
Panel border or faint line color.
```

### `progress_bg`

Default:

```json
"progress_bg": "#202020"
```

Purpose:

```text
Progress bar background.
```

### `progress_fg`

Default:

```json
"progress_fg": "#00FF00"
```

Purpose:

```text
Progress bar fill.
```

For a more monochrome UI:

```json
"progress_fg": "#FFFFFF"
```

### `volume_bar`

Default:

```json
"volume_bar": "#FFFFFF"
```

Purpose:

```text
Right-side vertical volume bar color.
```

Current design asks for white.

### `album_placeholder`

Default:

```json
"album_placeholder": "#181818"
```

Purpose:

```text
Placeholder block color if a generic album-art placeholder is needed.
```

Spotify should normally use trusted album art directly.

AirPlay should not use album art.

## Minimal safe config

For normal tuning, this is enough:

```json
{
  "display": {
    "fb_path": "/dev/fb0",
    "width": 480,
    "height": 320,
    "clock_type": "12h",
    "return_to_clock_seconds": 5.0
  },
  "ui": {
    "padding": 24,
    "volume_bar_width": 20,
    "volume_bar_height": 228,
    "volume_bar_right_margin": 32,
    "album_art_size": 150,
    "title_font_size": 26,
    "idle_clock_font_size": 132
  },
  "colors": {
    "background": "#000000",
    "text_main": "#FFFFFF",
    "text_dim": "#AAAAAA",
    "progress_fg": "#00FF00",
    "volume_bar": "#FFFFFF"
  }
}
```

Missing values fall back to defaults.

## Low-budget LCD recommendations

For a slower or lower-quality LCD:

```json
{
  "visual": {
    "colon_alpha_fade_hz": 0.5,
    "colon_alpha_min": 48,
    "colon_alpha_max": 255,
    "album_background_brightness_percent": 35
  },
  "ui": {
    "progress_bar_height": 8,
    "volume_bar_width": 24,
    "volume_overlay_seconds": 2
  },
  "colors": {
    "progress_fg": "#FFFFFF",
    "volume_bar": "#FFFFFF"
  }
}
```

Reason:

```text
slower colon fade
higher minimum alpha
darker background
plain white progress/volume elements
```

## Larger LCD recommendations

For a larger display:

```json
{
  "display": {
    "width": 800,
    "height": 480
  },
  "ui": {
    "padding": 32,
    "album_art_size": 210,
    "title_font_size": 34,
    "artist_font_size": 24,
    "album_font_size": 20,
    "idle_clock_font_size": 180,
    "idle_date_font_size": 28,
    "idle_ip_font_size": 20,
    "volume_bar_width": 28,
    "volume_bar_height": 320
  }
}
```

## What JSON should not control

Do not add JSON controls for:

```text
MPD usage
Spotify truth rules
AirPlay metadata rules
ALSA truth logic
whether AirPlay uses album art
pause detection
source-specific state machines
```

Reason:

```text
JSON controls presentation.
C code controls truth.
```

## Service-level notes

The blinking Linux console cursor is not a JSON option.

Keep this in the systemd service:

```ini
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true'
```

If the Volumio console banner appears on the LCD, also unbind fbcon in the service:

```ini
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind 2>/dev/null || true'
```

These are OS/framebuffer ownership issues, not display configuration.

## Restart after changes

After editing:

```bash
sudo nano /etc/volumio_fbd_config.json
sudo systemctl restart volumio_fbd.service
```

If the service file was edited:

```bash
sudo systemctl daemon-reload
sudo systemctl restart volumio_fbd.service
```

## Quick validation

Check JSON syntax before restart:

```bash
python3 -m json.tool /etc/volumio_fbd_config.json >/dev/null && echo OK
```

If it prints `OK`, the JSON is valid.
