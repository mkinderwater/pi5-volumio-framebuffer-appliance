# Raspberry Pi 5 Volumio Framebuffer Appliance

Minimal setup for a Raspberry Pi 5 Volumio player with a 3.5 inch GPIO LCD.

This assumes a fresh bare-metal Volumio install.

No desktop.  
No browser.  
No X11.  
No vendor LCD installer.  
No full GitHub clone.

Custom boot settings go here:

```text
/boot/userconfig.txt
```

Do not run these LCD installer scripts:

```bash
sudo ./MHS35IPS-show
sudo ./LCD35-show
sudo ./LCD35B-show
sudo ./LCD35B-show V2
```

Only the LCD overlay, SPI, framebuffer timing, display binary, and systemd service are needed.

## 1. LCD Setup

Pick one LCD section only.

Use SunFounder for the current LCD.  
Use TFT35A for the older Waveshare-compatible LCD.

Reboot after changing LCD boot settings.

## Current LCD: SunFounder MHS35IPS

Download the overlay:

```bash
sudo apt update
sudo apt install -y wget ca-certificates

sudo mkdir -p /boot/overlays

cd /tmp
wget -O mhs35ips-overlay.dtb \
https://raw.githubusercontent.com/sunfounder/LCD-show/master/usr/mhs35ips-overlay.dtb

sudo cp mhs35ips-overlay.dtb /boot/overlays/mhs35ips.dtbo
```

Append the boot settings:

```bash
sudo tee -a /boot/userconfig.txt >/dev/null <<'EOF_BOOT'

# MHS35IPS framebuffer
hdmi_force_hotplug=1
dtparam=spi=on
dtoverlay=mhs35ips:rotate=270
hdmi_group=2
hdmi_mode=87
hdmi_cvt 480 320 60 6 0 0 0
hdmi_drive=2
disable_overscan=1
framebuffer_width=480
framebuffer_height=320
EOF_BOOT
```

Default rotation:

```text
dtoverlay=mhs35ips:rotate=270
```

Alternate rotation:

```text
dtoverlay=mhs35ips:rotate=90
```

Reboot:

```bash
sudo reboot
```

## Older LCD: TFT35A / Older Waveshare-Compatible LCD

Download the overlay:

```bash
sudo apt update
sudo apt install -y wget ca-certificates

sudo mkdir -p /boot/overlays

cd /tmp
wget -O tft35a-overlay.dtb \
https://raw.githubusercontent.com/goodtft/LCD-show/master/usr/tft35a-overlay.dtb

sudo cp tft35a-overlay.dtb /boot/overlays/tft35a.dtbo
```

Append the boot settings:

```bash
sudo tee -a /boot/userconfig.txt >/dev/null <<'EOF_BOOT'

# TFT35A framebuffer
hdmi_force_hotplug=1
dtparam=spi=on
dtoverlay=tft35a:rotate=270
max_usb_current=1
hdmi_group=2
hdmi_mode=87
hdmi_cvt 480 320 60 6 0 0 0
hdmi_drive=2
disable_overscan=1
framebuffer_width=480
framebuffer_height=320
EOF_BOOT
```

Default rotation:

```text
dtoverlay=tft35a:rotate=270
```

Alternate rotation:

```text
dtoverlay=tft35a:rotate=90
```

Reboot:

```bash
sudo reboot
```

## 2. Install Display Program From GitHub

Install the compiled binary:

```bash
sudo apt update
sudo apt install -y wget ca-certificates

sudo wget -O /usr/local/bin/volumio_fbd \
https://raw.githubusercontent.com/mkinderwater/pi5-volumio-framebuffer-appliance/main/lcd%20program/compiled/volumio_fbd

sudo chmod 755 /usr/local/bin/volumio_fbd
```

Test `/dev/fb0` first:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

If needed, test `/dev/fb1`:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb1
```

Use whichever framebuffer works in the service.

## 3. Verify Framebuffer

After reboot and binary install:

```bash
ls -l /dev/fb*
cat /sys/class/graphics/fb0/name
```

If `/dev/fb1` exists, check it too:

```bash
cat /sys/class/graphics/fb1/name
```

## 4. Optional: Build From Source

Use this only if you want to compile it on the Pi.

```bash
sudo apt update
sudo apt install -y wget ca-certificates build-essential pkg-config libcurl4-openssl-dev libjson-c-dev libfreetype6-dev

mkdir -p /tmp/volumio-fbd
cd /tmp/volumio-fbd

wget -O volumio_fbd.c \
https://raw.githubusercontent.com/mkinderwater/pi5-volumio-framebuffer-appliance/main/lcd%20program/source/volumio_fbd.c

gcc -Os -pipe -Wall -Wextra -pthread \
  -ffunction-sections -fdata-sections \
  $(pkg-config --cflags freetype2) \
  volumio_fbd.c \
  -o volumio_fbd \
  $(pkg-config --libs freetype2 libcurl json-c) \
  -lm -Wl,--gc-sections -s

sudo cp volumio_fbd /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd
```

Test it:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

## 5. Create Systemd Service

Create the service:

```bash
sudo nano /etc/systemd/system/volumio-fbd.service
```

Paste:

```ini
[Unit]
Description=Volumio framebuffer display
After=network-online.target volumio.service
Wants=network-online.target

[Service]
Type=simple

# Disable the Linux console cursor blink.
# Without this, a blinking cursor may appear over the framebuffer display.
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true'

# Detach the Linux virtual console from the LCD framebuffer when available.
# This helps prevent console text or cursor artifacts from showing on the screen.
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind 2>/dev/null || true'

ExecStart=/usr/local/bin/volumio_fbd /dev/fb0
Restart=always
RestartSec=2
User=root
TimeoutStopSec=3
KillMode=control-group

[Install]
WantedBy=multi-user.target
```

If the LCD uses `/dev/fb1`, change:

```ini
ExecStart=/usr/local/bin/volumio_fbd /dev/fb0
```

to:

```ini
ExecStart=/usr/local/bin/volumio_fbd /dev/fb1
```

Enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable volumio-fbd
sudo systemctl start volumio-fbd
sudo systemctl status volumio-fbd
```

Restart after replacing the binary:

```bash
sudo systemctl restart volumio-fbd
```

## 6. Upgrade to the Latest Release

Use this if you want to upgrade to the latest release.

```bash
sudo systemctl stop volumio-fbd 2>/dev/null || true

sudo wget -O /usr/local/bin/volumio_fbd \
https://raw.githubusercontent.com/mkinderwater/pi5-volumio-framebuffer-appliance/main/lcd%20program/compiled/volumio_fbd

sudo chmod 755 /usr/local/bin/volumio_fbd
sudo systemctl restart volumio-fbd
```

This does not overwrite your local settings.

## 7. JSON Settings

The display program creates this file the first time it runs:

```text
/etc/volumio_fbd_config.json
```

Edit it here:

```bash
sudo nano /etc/volumio_fbd_config.json
```

Restart the service after changes:

```bash
sudo systemctl restart volumio-fbd
```

## Display Settings

These control the framebuffer device, screen size, clock format, and how quickly the display returns to the clock screen.

```json
"display": {
  "fb_path": "/dev/fb0",
  "width": 480,
  "height": 320,
  "clock_type": "12h",
  "return_to_clock_seconds": 5.0
}
```

### `fb_path`

Framebuffer device used by the LCD.

Use this first:

```json
"fb_path": "/dev/fb0"
```

Use this only if the LCD works on `/dev/fb1`:

```json
"fb_path": "/dev/fb1"
```

### `width` and `height`

LCD resolution.

For the 3.5 inch LCDs used here:

```json
"width": 480,
"height": 320
```

Do not change these unless you are using a different display.

### `clock_type`

Clock format.

Use 12-hour time:

```json
"clock_type": "12h"
```

Use 24-hour time:

```json
"clock_type": "24h"
```

### `return_to_clock_seconds`

How long the display waits before returning to the large clock screen when playback is idle or stopped.

Example:

```json
"return_to_clock_seconds": 5.0
```

Higher values wait longer.

## Visual Settings

These control clock animation and album art background behaviour.

```json
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
}
```

### `colon_alpha_fade_enabled`

Enables the soft fade effect on the clock colon.

```json
"colon_alpha_fade_enabled": true
```

Turn it off:

```json
"colon_alpha_fade_enabled": false
```

### `colon_alpha_fade_hz`

Speed of the colon fade.

```json
"colon_alpha_fade_hz": 0.5
```

Lower is slower.  
Higher is faster.

### `colon_alpha_min` and `colon_alpha_max`

Controls how dim and bright the fading colon gets.

```json
"colon_alpha_min": 32,
"colon_alpha_max": 255
```

`0` is invisible.  
`255` is fully visible.

### `album_background_enabled`

Uses album art as the screen background while music is playing.

```json
"album_background_enabled": true
```

Turn it off for a plain background:

```json
"album_background_enabled": false
```

### `album_background_zoom_percent`

Controls how much the background album art is enlarged.

```json
"album_background_zoom_percent": 220
```

Higher values zoom in more.

### `album_background_brightness_percent`

Controls how bright the album art background appears.

```json
"album_background_brightness_percent": 42
```

Lower is darker.  
Higher is brighter.

### `album_background_diagonal_fade_when_title_scrolls`

Adds a diagonal fade when long track titles scroll.

```json
"album_background_diagonal_fade_when_title_scrolls": true
```

This helps keep scrolling text readable.

### `album_background_diagonal_fade_start` and `album_background_diagonal_fade_end`

Controls where the diagonal fade begins and ends.

```json
"album_background_diagonal_fade_start": 96,
"album_background_diagonal_fade_end": 128
```

Most users should leave these unchanged.

## UI Layout Settings

These control spacing, font sizes, album art size, the progress bar, the volume bar, and AirPlay icon sizing.

```json
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
}
```

### `padding`

Outer spacing around the screen content.

```json
"padding": 24
```

Lower values give more room.  
Higher values add more margin.

### `footer_height` and `header_height`

Reserved space for the top and bottom screen areas.

```json
"footer_height": 24,
"header_height": 32
```

### `progress_bar_height`

Height of the playback progress bar.

```json
"progress_bar_height": 7
```

### `volume_bar_width`, `volume_bar_height`, and `volume_bar_right_margin`

Controls the vertical volume bar.

```json
"volume_bar_width": 20,
"volume_bar_height": 228,
"volume_bar_right_margin": 32
```

### `volume_overlay_seconds`

How long the volume overlay stays visible after volume changes.

```json
"volume_overlay_seconds": 2
```

### `album_art_size`

Size of normal album art.

```json
"album_art_size": 150
```

### `generic_album_art_size`

Size of the fallback album icon when no album image is available.

```json
"generic_album_art_size": 136
```

### Font sizes

These control text size for each display area.

```json
"title_font_size": 26,
"artist_font_size": 18,
"album_font_size": 15,
"source_font_size": 14,
"footer_font_size": 11,
"small_clock_font_size": 16,
"idle_clock_font_size": 132,
"idle_date_font_size": 20,
"idle_ip_font_size": 16
```

Most useful values to adjust:

```json
"title_font_size": 26
```

```json
"idle_clock_font_size": 132
```

Lower the number if text is too large.  
Raise it if text is too small.

### `airplay_icon_width` and `airplay_icon_height`

Controls the AirPlay icon size.

```json
"airplay_icon_width": 132,
"airplay_icon_height": 78
```

## Color Settings

Colors use standard hex color values.

```json
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
```

### `background`

Main screen background.

```json
"background": "#000000"
```

### `text_main`

Primary text color.

```json
"text_main": "#FFFFFF"
```

### `text_dim`

Secondary text color.

```json
"text_dim": "#AAAAAA"
```

### `text_muted`

Muted text color for less important details.

```json
"text_muted": "#777777"
```

### `panel`

Panel background color.

```json
"panel": "#050505"
```

### `panel_line`

Panel border or divider line color.

```json
"panel_line": "#242424"
```

### `progress_bg`

Playback progress bar background.

```json
"progress_bg": "#202020"
```

### `progress_fg`

Playback progress bar fill color.

```json
"progress_fg": "#00FF00"
```

### `volume_bar`

Volume bar color.

```json
"volume_bar": "#FFFFFF"
```

### `album_placeholder`

Fallback album art background color.

```json
"album_placeholder": "#181818"
```

## Full Default Config

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
