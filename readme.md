# Raspberry Pi 5 Volumio Framebuffer Appliance

This is a small, purpose-built Raspberry Pi 5 Volumio appliance with M.2 storage, DAC output, PoE, and a minimal LCD status screen.

The hardware is only half the project. The other half is the display program, written in C.

Instead of running a desktop, browser, kiosk mode, or heavy plugin just to show basic music info, this build writes directly to the Linux framebuffer. No X11, Wayland, Chromium, or Chromium-based UI stack runs in the background.

On my Raspberry Pi 5, the display program is designed to stay extremely light, usually sitting under 1% CPU during normal use.

The goal is simple:

```text
Show useful Volumio information without wasting the Pi.
```

## Design Philosophy

This is not a touchscreen kiosk.

It is a small audio appliance display.

The screen should show what matters, stay readable on a small LCD, and avoid pretending unreliable metadata is reliable.

The project uses this truth model:

```text
Volumio = source, metadata, artwork, volume
ALSA status = whether audio is actually playing
ALSA hw_params = actual DAC/output format
JSON config = user-tunable display behavior
```

MPD is not used in the production display path. Testing showed that MPD did not reliably expose Spotify or AirPlay playback on this setup, while Volumio did.

## Display Features

The LCD displays:

- Large idle clock
- Date
- Device IP address
- Spotify playback screen
- Spotify song title
- Spotify artist
- Spotify album
- Spotify album artwork
- Spotify progress bar
- Scrolling song title when needed
- Dimmed Spotify album-art background
- Diagonal background fade when the title scrolls
- Clean AirPlay source screen
- Actual ALSA output format
- Right-side vertical volume bar
- Blinking colon heartbeat effect
- Return-to-clock behavior based on actual ALSA audio activity

## Idle Screen

When nothing is playing, the big clock screen shows only:

```text
Clock
Date
Device IP address
```

No extra footer.  
No ALSA status text.  
No source labels.  
No stale album art.  
No background image.

The idle screen is intentionally clean.

## Spotify Screen

Spotify is treated as the reliable rich-media source.

Spotify displays:

- Album art
- Song title
- Artist
- Album
- Progress bar
- Scrolling title if needed
- Actual output format
- Volume overlay when volume changes

Spotify album art is trusted directly from Volumio because Spotify provides stable artwork URLs.

Typical Spotify artwork:

```text
https://i.scdn.co/image/...
```

No fallback artwork is used for Spotify.

## AirPlay Screen

AirPlay metadata and artwork are inconsistent through Volumio, so AirPlay gets a clean source screen instead of a fake media screen.

AirPlay displays:

- Centered AirPlay-style icon
- Actual ALSA output format if available
- Right-side volume bar when volume changes

AirPlay does not display:

- Title
- Artist
- Album
- Album art
- Progress bar
- Pause state

This is intentional. It keeps the UI accurate instead of showing stale or guessed information.

## Audio Truth

The program does not rely on Volumio alone to decide whether audio is actually playing.

Playback activity comes from ALSA:

```text
/proc/asound/card*/pcm*/sub*/status
```

If any active playback status contains:

```text
state: RUNNING
```

the display treats audio as active.

When ALSA is no longer running, the return-to-clock timer starts.

## Output Format Truth

The program reads ALSA hardware output format from:

```text
/proc/asound/card*/pcm*/sub*/hw_params
```

This allows the LCD to show the actual output format reaching the DAC.

Example ALSA output:

```text
format: S32_LE
channels: 2
rate: 44100 (44100/1)
```

Displayed as:

```text
44.1/32/St
```

This is hardware output truth, not guessed media metadata.

## Album Background Behavior

Spotify can show a zoomed, dimmed album-art background.

The behavior depends on whether the song title scrolls:

```text
If the title does not scroll:
  show the full dimmed zoomed album background

If the title scrolls:
  fade the background to black at the 50% diagonal
```

This keeps long scrolling titles readable.

AirPlay never uses album background.

The big clock never uses album background.

## Volume Display

Volume feedback is unified across active sources.

When volume changes, the display shows:

- A right-side vertical bar
- White fill
- Current volume value
- Fade-out after a short delay

The volume bar is intentionally simple and readable on a low-cost LCD.

## JSON Configuration

The display is configured through:

```text
/etc/volumio_fbd_config.json
```

The JSON file controls display behavior and visual tuning without recompiling.

It can control:

- Framebuffer path
- Render width
- Render height
- Clock format
- Return-to-clock delay
- Font sizes
- Padding
- Album art size
- Volume bar size
- Colors
- Colon fade speed
- Album background behavior

It should not control playback truth.

Playback truth remains in code:

```text
Volumio = metadata/source
ALSA = audio truth
```

Example config:

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
    "progress_bar_height": 7,
    "volume_bar_width": 20,
    "volume_bar_height": 228,
    "volume_bar_right_margin": 32,
    "volume_overlay_seconds": 2,
    "album_art_size": 150,
    "title_font_size": 26,
    "artist_font_size": 18,
    "album_font_size": 15,
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
    "progress_bg": "#202020",
    "progress_fg": "#00FF00",
    "volume_bar": "#FFFFFF"
  }
}
```

## Performance Design

The display program avoids unnecessary work.

Current performance principles:

- Use Volumio polling in a background thread
- Keep network and image decoding off the render loop
- Cache FreeType glyphs
- Cache scaled album art
- Cache scrolling title strips
- Cache zoomed album background
- Use dirty framebuffer spans
- Use accurate frame pacing
- Cache active ALSA status paths
- Cache active ALSA hw_params paths

The render loop should draw from prepared state, not wait on network calls or JPEG decoding.

## Hardware Stack

The enclosure was designed around the actual hardware stack:

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- NVMe storage
- InnoMaker HiFi DAC Pro
- SunFounder MHS35IPS 3.5 inch IPS LCD

## As Built

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- 128GB NVMe M.2 storage
- InnoMaker HiFi DAC Pro
- SunFounder MHS35IPS 3.5 inch IPS LCD

## Framebuffer Display Setup

For a fresh Volumio install, do not run the full `LCD-show` installer.

The full installer adds extra packages and files that are not needed for this project. This build only needs the framebuffer display overlay and the correct boot configuration.

Download only the overlay:

```bash
sudo mkdir -p /boot/overlays

cd /tmp
wget -O mhs35ips-overlay.dtb \
https://raw.githubusercontent.com/sunfounder/LCD-show/master/usr/mhs35ips-overlay.dtb

sudo cp mhs35ips-overlay.dtb /boot/overlays/mhs35ips.dtbo
```

Back up the Volumio user boot config:

```bash
sudo cp /boot/userconfig.txt /boot/userconfig.txt.bak.$(date +%F-%H%M) 2>/dev/null || true
```

Remove any previous iCube LCD block:

```bash
sudo sed -i '/# iCube MHS35IPS framebuffer start/,/# iCube MHS35IPS framebuffer end/d' /boot/userconfig.txt 2>/dev/null || true
```

Append the minimal framebuffer configuration:

```bash
sudo tee -a /boot/userconfig.txt >/dev/null <<'EOF'

# iCube MHS35IPS framebuffer start
hdmi_force_hotplug=1
dtparam=spi=on
dtoverlay=mhs35ips:rotate=90
hdmi_group=2
hdmi_mode=87
hdmi_cvt 480 320 60 6 0 0 0
hdmi_drive=2
disable_overscan=1
framebuffer_width=480
framebuffer_height=320
# iCube MHS35IPS framebuffer end
EOF
```

Reboot:

```bash
sudo reboot
```

After reboot, verify the framebuffer:

```bash
ls -l /dev/fb*
fbset -fb /dev/fb0 -i
cat /sys/class/graphics/fb0/name
```

If the display is rotated wrong, edit this line in `/boot/userconfig.txt`:

```text
dtoverlay=mhs35ips:rotate=90
```

Try:

```text
dtoverlay=mhs35ips:rotate=270
```

Then reboot.

## Enclosure

The OpenSCAD case includes:

- USB-C cutout
- HDMI cutouts
- Audio jack cutouts
- Ethernet cutout
- USB cutouts
- MicroSD access
- Bottom intake vents
- Side fin vents
- Stronger M2.5 mounting points
- Lid screw mounts
- DAC jack labels
- Solid sticky-foot pads for side orientation

A lot of Raspberry Pi Volumio builds are bare boards, generic cases, or full touchscreen setups running more software than needed.

This one is built more like a simple audio appliance.

## Build

Install dependencies:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libcurl4-openssl-dev libjson-c-dev libfreetype6-dev
```

Build:

```bash
gcc -Os -pipe -Wall -Wextra -pthread \
  -ffunction-sections -fdata-sections \
  $(pkg-config --cflags freetype2) \
  volumio_fbd.c \
  -o volumio_fbd \
  $(pkg-config --libs freetype2 libcurl json-c) \
  -lm -Wl,--gc-sections -s
```

Install:

```bash
sudo systemctl stop volumio-fbd.service 2>/dev/null || true
sudo cp -f volumio_fbd /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd
```

Test manually:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

Stop the manual test with `Ctrl+C`.

## Systemd Service

Create the service:

```bash
sudo nano /etc/systemd/system/volumio-fbd.service
```

Paste:

```ini
[Unit]
Description=iCube Volumio Framebuffer Display
After=network-online.target volumio.service
Wants=network-online.target

[Service]
Type=simple
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true'
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind 2>/dev/null || true'
ExecStart=/usr/local/bin/volumio_fbd /dev/fb0
Restart=always
RestartSec=3
User=root
WorkingDirectory=/usr/local/bin
TimeoutStopSec=3
KillMode=control-group

[Install]
WantedBy=multi-user.target
```

Reload and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable volumio-fbd.service
sudo systemctl start volumio-fbd.service
sudo systemctl status volumio-fbd.service
```

If the LCD appears as `/dev/fb1`, change only this line:

```ini
ExecStart=/usr/local/bin/volumio_fbd /dev/fb1
```

Then reload and restart:

```bash
sudo systemctl daemon-reload
sudo systemctl restart volumio-fbd.service
```

## Console Notes

The Linux framebuffer console can draw over the LCD if it remains attached.

The blinking console cursor is not a C code issue. It is a kernel framebuffer console setting.

The service disables the cursor blink before starting the display:

```ini
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true'
```

If the Volumio console banner appears on the LCD, the service also attempts to unbind fbcon:

```ini
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind 2>/dev/null || true'
```

## What This Is

This is not trying to be a commercial hi-fi unit.

It is a clean Raspberry Pi 5 Volumio player with:

- M.2 NVMe storage
- DAC output
- PoE support
- Minimal LCD display
- Purpose-built enclosure
- Low-overhead framebuffer display software

It does one job well.

## What This Is Not

This is not:

- A Chromium kiosk
- A desktop UI
- A touchscreen-first interface
- A Volumio plugin replacement
- A general-purpose media dashboard
- A commercial product

It is a simple appliance display for a specific Volumio build.

## Included Files

This repository includes:

- Source files
- OpenSCAD design files
- C display program
- Compiled binary
- Install notes
- JSON configuration notes
- Bill of materials

## Production Notes

The production display binary should stay clean.

Do not re-add:

- MPD playback logic
- AirPlay pause guessing
- Spotify fallback artwork
- Custom socket HTTP stack
- Debug logging in production
- Complex source-specific state machines

Keep the truth split:

```text
Volumio tells us what is playing.
ALSA tells us whether audio is moving.
JSON tells us how the display should look.
```
