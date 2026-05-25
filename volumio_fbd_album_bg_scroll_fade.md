# Volumio FBD Visual Rule: Album Background and Scrolling Title Fade

Date: 2026-05-24  
Scope: Spotify / trusted rich playback screen only

## Rule summary

The album background behavior depends on whether the song title scrolls.

```text
If the song title does not scroll:
  show the full zoomed album background

If the song title scrolls:
  show the zoomed album background,
  but fade it to black at the 50% diagonal
```

Plain meaning:

```text
Short title:
  pretty full background

Long scrolling title:
  background gets pushed back so the title stays readable
```

## Source rule

This visual rule only applies to trusted rich sources.

Allowed:

```text
Spotify
trusted album art sources
```

Not allowed:

```text
AirPlay
generic /albumart
fallback art
placeholder art
idle clock
```

AirPlay remains a clean source screen:

```text
AirPlay
Volume
ALSA output format
```

No AirPlay title, album art, progress, or album background.

## Why this exists

Scrolling text needs contrast.

A full album-art background behind a moving title can make the title harder to read.

The fade rule keeps the visual style, but gives the scrolling title a darker side of the screen.

## Expected visual behavior

### Title does not scroll

```text
+------------------------------------------------+
|                                                |
|      full dimmed zoomed album background       |
|                                                |
|  Album Art     Title                           |
|                Artist                          |
|                Album                           |
|                                                |
|                Progress                        |
+------------------------------------------------+
```

### Title scrolls

```text
+------------------------------------------------+
| album background visible                       |
| album background visible       fade -> black   |
| album background visible       fade -> black   |
| Album Art     Scrolling Title  black protected |
|               Artist           black protected |
|               Album            black protected |
|                                                |
|               Progress                         |
+------------------------------------------------+
```

The fade should follow a diagonal, not a flat vertical block.

## Decision logic

The render code should first measure the title width.

```c
bool title_scrolls = title_width > title_clip_width;
```

Then pass that into the background renderer:

```c
draw_album_art_background_zoomed(title_scrolls);
```

Meaning:

```c
draw_album_art_background_zoomed(false);
```

for short titles.

```c
draw_album_art_background_zoomed(true);
```

for long scrolling titles.

## Background function contract

Recommended function shape:

```c
static void draw_album_art_background_zoomed(bool apply_diagonal_fade) {
    /*
      Uses trusted album art only.
      Builds or reuses cached zoomed background.
      If apply_diagonal_fade is false:
        draw full dimmed background.
      If apply_diagonal_fade is true:
        apply diagonal fade ending at the 50% point.
    */
}
```

## Background brightness

The background should always be dimmed.

Recommended default:

```json
{
  "visual": {
    "album_background_brightness_percent": 42
  }
}
```

Meaning:

```text
Album background is visible,
but never competes with foreground text.
```

## 50% diagonal fade rule

Use a 0 to 255 diagonal scale.

```text
0   = lower-left side
128 = 50% diagonal point
255 = upper-right side
```

Fade should start before the midpoint and reach black at the midpoint.

Recommended constants:

```c
const int diagonal_fade_start = 96;
const int diagonal_fade_end   = 128;
const int keep_floor          = 0;
```

Meaning:

```text
0..95:
  keep normal dimmed background

96..127:
  fade down

128+:
  black
```

## Diagonal position math

Each pixel gets a diagonal score.

```c
int nx_255 = (x * 255) / width_denom;
int inv_y_255 = ((height - 1 - y) * 255) / height_denom;
int diag = (nx_255 + inv_y_255) / 2;
```

This creates a bottom-left to top-right diagonal fade.

Then calculate how much background to keep:

```c
int keep = 255;

if (apply_diagonal_fade) {
    if (diag <= diagonal_fade_start) {
        keep = 255;
    } else if (diag >= diagonal_fade_end) {
        keep = keep_floor;
    } else {
        int span = diagonal_fade_end - diagonal_fade_start;
        int pos = diag - diagonal_fade_start;
        keep = 255 - ((255 - keep_floor) * pos / span);
    }
}
```

With `keep_floor = 0`, the background becomes fully black at the 50% diagonal.

## Pixel output math

The dimmed album-art pixel is multiplied by `keep`.

```c
dst[0] = (br  * keep) / 255;
dst[1] = (bgc * keep) / 255;
dst[2] = (bb  * keep) / 255;
```

Where:

```text
br  = dimmed red channel
bgc = dimmed green channel
bb  = dimmed blue channel
```

So:

```text
keep = 255:
  full dimmed background

keep = 128:
  half of the dimmed background

keep = 0:
  black
```

## Cache rule

The zoomed album background should be cached.

Rebuild only when one of these changes:

```text
album art generation changes
screen width/height changes
background fade mode changes
background brightness changes
```

Do not rebuild the background every frame.

Normal frames should copy the cached background into the framebuffer draw buffer.

```c
memcpy(g_img, g_album_bg_rgb, g_width * g_height * 3);
```

or equivalent.

## Config truth

Recommended JSON:

```json
{
  "visual": {
    "album_background_enabled": true,
    "album_background_sources": ["spotify"],
    "album_background_zoom_percent": 220,
    "album_background_brightness_percent": 42,
    "album_background_diagonal_fade_when_title_scrolls": true,
    "album_background_diagonal_fade_start": 96,
    "album_background_diagonal_fade_end": 128
  }
}
```

Default behavior:

```text
album_background_enabled = true
album_background_sources = spotify only
album_background_zoom_percent = 220
album_background_brightness_percent = 42
album_background_diagonal_fade_when_title_scrolls = true
album_background_diagonal_fade_start = 96
album_background_diagonal_fade_end = 128
```

## Render order

Recommended draw order for Spotify:

```text
1. Clear screen to black
2. Draw cached album background
3. Draw album art tile
4. Draw title / scrolling title
5. Draw artist
6. Draw album
7. Draw progress bar
8. Draw volume / source / output format
9. Flush to framebuffer
```

If no trusted background exists:

```text
1. Clear screen to black
2. Draw normal foreground UI
```

## Scroll interaction

The title scroll state determines the background mode.

```c
bool title_scrolls = text_width(title) > available_title_width;
```

Then:

```c
bool fade_background =
    cfg.album_background_diagonal_fade_when_title_scrolls &&
    title_scrolls;
```

Then:

```c
draw_album_art_background_zoomed(fade_background);
```

## Important rules

Do not use album background on the big clock.

```text
Big clock = black background only
```

Do not use album background for AirPlay.

```text
AirPlay = clean source screen only
```

Do not use album background from placeholder art.

```text
No /albumart generic placeholder
No pulse fallback
No default art
```

Do not let visual state affect playback state.

```text
ALSA still decides whether audio is active.
Volumio still provides source/metadata.
JSON still controls display timing.
```

## Final truth

```text
Short Spotify title:
  full dimmed zoomed album background

Long Spotify title:
  dimmed zoomed album background,
  diagonal fade to black by the 50% point

AirPlay:
  no album background

Idle clock:
  no album background
```

## Design principle

Readable first. Pretty second.

The background should add character, not fight the information.
