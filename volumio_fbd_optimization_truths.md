# Volumio FBD Optimization Truths

Date: 2026-05-24  
Scope: rendering and performance architecture

## Purpose

These are accepted performance truths for the framebuffer display code.

They describe how the code should avoid wasting CPU, memory bandwidth, and framebuffer bus time.

These are optimization truths only. They must not change playback/source truth.

```text
Volumio = source / metadata
ALSA status = audio activity
ALSA hw_params = hardware output format
JSON = configuration
Optimization layer = efficient rendering only
```

## 1. FreeType Glyph Caching

### Prototype behavior

High CPU path:

```text
Call FT_Load_Char inside the render loop.
Rasterize every character repeatedly.
Repeat this 30 times per second during scrolling/animation.
```

Problem:

```text
FreeType glyph rasterization is expensive.
Re-rendering the same letters every frame wastes CPU.
```

### Optimized behavior

Low CPU path:

```text
Render each glyph once.
Store the glyph alpha bitmap in a GlyphAtlas.
Draw text using cached alpha maps.
```

Drawing text becomes:

```text
lookup glyph
copy/blend cached alpha bitmap
advance cursor
```

### Required implementation idea

Use a glyph cache keyed by:

```text
font face
font size
Unicode codepoint
```

Example structure:

```c
typedef struct {
    bool valid;
    uint32_t codepoint;
    int width;
    int height;
    int pitch;
    int advance;
    int left;
    int top;
    uint8_t *alpha;
} GlyphCacheEntry;
```

### Truth rule

```text
Never rasterize the same glyph every frame.
FT_Load_Char belongs in the cache-miss path only.
```

## 2. Scaled Artwork Caching

### Prototype behavior

High CPU path:

```text
For every frame:
  loop over the full-resolution album art
  rescale pixels
  recalculate crop/zoom math
  blend into the display buffer
```

Problem:

```text
Album art scaling is expensive.
Doing it every frame wastes CPU.
```

### Optimized behavior

Low CPU path:

```text
Scale the artwork once.
Store the result in g_art_scaled_rgb.
Render frames by copying the pre-scaled block.
```

### Required implementation idea

Cache should be invalidated only when:

```text
album art changes
target art size changes
screen geometry changes
```

Example state:

```c
static uint8_t *g_art_scaled_rgb = NULL;
static int g_cached_art_w = 0;
static int g_cached_art_h = 0;
static unsigned long g_art_scaled_generation = 0;
```

### Truth rule

```text
Artwork scaling belongs in the cache-build path.
Normal frames should memcpy or copy from g_art_scaled_rgb.
```

## 3. Album Background Caching

### Prototype behavior

High CPU path:

```text
For every frame:
  zoom album art
  sample source pixels
  dim background
  calculate diagonal fade
  write full background
```

Problem:

```text
The zoomed background changes rarely.
Rebuilding it every frame is wasteful.
```

### Optimized behavior

Low CPU path:

```text
Build the zoomed/dimmed/faded background once.
Store it in a cached background buffer.
Copy it into the frame when needed.
```

### Rebuild only when

```text
album art generation changes
framebuffer width/height changes
background brightness changes
background zoom changes
fade mode changes
```

### Truth rule

```text
The Spotify album background must be cached.
The render loop should not recalculate the zoomed background per frame.
```

## 4. Scroll-Strip Buffering

### Prototype behavior

High CPU path:

```text
Re-rasterize the scrolling song title every frame.
Render the text twice per frame to create a marquee loop.
```

Problem:

```text
Scrolling text is visually dynamic, but the text bitmap itself does not change every frame.
Only the visible window offset changes.
```

### Optimized behavior

Low CPU path:

```text
Render the full scrolling title sequence once into g_scroll_strip_rgb.
Each frame copies a moving window from that strip.
```

The strip should include:

```text
title text
gap
title text repeated for seamless loop
```

### Required implementation idea

Cache key should include:

```text
title string
font face
font size
text color
clip width
gap width
```

Example state:

```c
static uint8_t *g_scroll_strip_rgb = NULL;
static int g_scroll_strip_w = 0;
static int g_scroll_strip_h = 0;
static int g_scroll_strip_cycle_w = 0;
static char g_scroll_strip_text[256] = "";
```

### Truth rule

```text
Scrolling should move a bitmap window.
It should not re-rasterize text every frame.
```

## 5. Framebuffer Dirty Spans

### Prototype behavior

High CPU path:

```text
Flush the full framebuffer every loop.
```

For a 480x320 display:

```text
480 * 320 = 153,600 pixels
```

Even if only the clock colon changed, the prototype still pushes the whole frame.

Problem:

```text
Full-frame blits waste memory bandwidth.
On slower LCD buses, this can limit FPS and increase CPU use.
```

### Optimized behavior

Low CPU path:

```text
Compare the new frame to the previous frame.
Build a dirty span map.
Only copy changed horizontal pixel spans to /dev/fb0.
```

### Required implementation idea

One row-span entry per display row:

```c
typedef struct {
    int min_x;
    int max_x;
    bool dirty;
} RowSpanTracker;
```

Per row:

```text
if pixels differ:
  mark min_x/max_x
```

Flush only those spans.

### Truth rule

```text
Do not push the full framebuffer when only small areas changed.
Use dirty spans for partial updates.
```

## 6. Dynamic Framerate Pacing

### Prototype behavior

Simple but wasteful path:

```text
sleep 33ms if title scrolls
sleep 200ms otherwise
```

Problem:

```text
Different screens need different update rates.
Idle screens do not need playback-rate redraws.
Playback without animation does not need 30 FPS.
```

### Optimized behavior

Scale framerate based on visual need:

```text
30 FPS:
  scrolling title
  visible animation
  fade effect

1 FPS:
  playback tracking
  clock/progress updates
  static media screen

0.2 FPS:
  deep idle
  no animation
  no active media
```

Suggested constants:

```c
#define HARDWARE_FPS_SCROLLING 30.0
#define HARDWARE_FPS_PLAYING    1.0
#define HARDWARE_FPS_STATIC     1.0
#define HARDWARE_FPS_SLEEP      0.2
```

### Wakeup truth

Deep sleep should not make the UI feel dead.

Optimized wake should use:

```text
condition variables
eventfd
or another signal/wakeup mechanism
```

When playback/source/audio state changes:

```text
wake render loop immediately
```

### Truth rule

```text
Render rate should follow visual need.
Idle should be cheap.
State changes should wake immediately.
```

## 7. Relationship to ALSA Truth

Optimization must not replace audio truth.

```text
ALSA status still decides audio running.
ALSA hw_params still decides output format.
Volumio still decides source/metadata.
```

Dirty spans, glyph cache, scaled art cache, and framerate pacing are only about efficient drawing.

## 8. Relationship to Visual Truth

The visual truth remains:

```text
Spotify:
  rich media screen
  album art
  scrolling title
  cached zoomed background
  diagonal fade when title scrolls

AirPlay:
  clean source screen
  no album art
  no title/artist/album
  no progress

Idle:
  black big clock
```

Optimization must support these visuals without adding false source assumptions.

## 9. What Not To Bring Back

Do not reintroduce these under the name of optimization:

```text
MPD as playback truth
AirPlay pause guessing
Spotify fallback art
custom HTTP socket stack
debug logging in production
large source-specific state machines
```

## 10. Target Rendering Model

Ideal render flow:

```text
1. Poll/copy truth state
2. Decide screen mode
3. Load/reuse glyph cache
4. Load/reuse scaled album art
5. Load/reuse album background
6. Load/reuse scroll strip
7. Draw into offscreen RGB buffer
8. Compare against previous frame
9. Flush only dirty spans
10. Sleep according to dynamic framerate
```

## Final truth

```text
Cache what is expensive.
Copy what is already prepared.
Flush only what changed.
Sleep whenever nothing useful is happening.
Wake immediately when truth changes.
```
