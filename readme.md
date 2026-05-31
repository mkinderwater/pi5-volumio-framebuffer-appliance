# Raspberry Pi 5 Volumio Framebuffer Appliance

A small Raspberry Pi 5 Volumio player with M.2 storage, DAC output, PoE, and a minimal 3.5 inch LCD status screen.

The display program is written in C and writes directly to the Linux framebuffer.

No desktop.  
No browser.  
No X11.  
No Wayland.  
No touch kiosk.  
No full LCD vendor installer.

The goal:

```text
Show useful Volumio information without wasting the Pi.
```

## Fresh Install Assumption

This guide assumes a fresh bare-metal Volumio install on a Raspberry Pi 5.

Custom boot settings go here:

```text
/boot/userconfig.txt
```

Do not run these unless you want extra desktop/touch/browser setup:

```bash
sudo ./MHS35IPS-show
sudo ./LCD35-show
sudo ./LCD35B-show
sudo ./LCD35B-show V2
```

This appliance only needs:

- LCD overlay
- SPI enabled
- 480x320 framebuffer timing
- `/dev/fb0` or `/dev/fb1`
- `volumio_fbd` systemd service

## Hardware Stack

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- 128GB NVMe M.2 storage
- InnoMaker HiFi DAC Pro
- 3.5 inch GPIO LCD

## Display Truth Model

```text
Volumio = source, metadata, artwork, volume
ALSA status = whether audio is actually playing
ALSA hw_params = actual DAC/output format
JSON config = display tuning
```

MPD is not used in the production display path.

Volumio gives the source and metadata.  
ALSA tells whether audio is actually moving.  
ALSA also tells the real output format reaching the DAC.

## Display Features

- Large idle clock
- Date
- Device IP address
- Spotify playback screen
- Spotify title, artist, album, and artwork
- Spotify progress bar
- Scrolling song title when needed
- Dimmed Spotify album-art background
- Diagonal background fade when the title scrolls
- Clean AirPlay source screen
- Actual ALSA output format
- Right-side vertical volume bar
- Blinking colon heartbeat effect
- Return-to-clock based on actual ALSA audio activity

## Idle Screen

When nothing is playing, the screen shows only:

```text
Clock
Date
Device IP address
```

No footer.  
No source label.  
No ALSA text.  
No stale album art.  
No background image.

## LCD Setup

Pick one LCD section only.

This guide assumes a fresh bare-metal Volumio install.

Use SunFounder for the current LCD.  
Use TFT35A for the older Waveshare-compatible LCD.

After changing LCD boot settings, reboot before testing the framebuffer app.

## Current LCD: SunFounder MHS35IPS

Download only the overlay:

```bash
sudo mkdir -p /boot/overlays

cd /tmp
wget -O mhs35ips-overlay.dtb \
https://raw.githubusercontent.com/sunfounder/LCD-show/master/usr/mhs35ips-overlay.dtb

sudo cp mhs35ips-overlay.dtb /boot/overlays/mhs35ips.dtbo
```

Append the Volumio boot block:

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

Alternate rotation if the screen is wrong:

```text
dtoverlay=mhs35ips:rotate=90
```

Reboot:

```bash
sudo reboot
```

## Older LCD: TFT35A / Older Waveshare-Compatible 3.5 Inch GPIO LCD

The older LCD used the `tft35a` overlay.

Use the GoodTFT git repo only to get the overlay file.

Do not run the installer script.

Download the overlay:

```bash
sudo apt update
sudo apt install -y git
sudo mkdir -p /boot/overlays

cd /tmp
git clone --depth 1 https://github.com/goodtft/LCD-show.git

sudo cp /tmp/LCD-show/usr/tft35a-overlay.dtb /boot/overlays/tft35a.dtbo
```

Append the Volumio boot block:

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

Alternate rotation if the screen is wrong:

```text
dtoverlay=tft35a:rotate=90
```

Reboot:

```bash
sudo reboot
```

## Verify Framebuffer

After reboot:

```bash
ls -l /dev/fb*
fbset -fb /dev/fb0 -i
cat /sys/class/graphics/fb0/name
```

Test the display app:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

If the LCD is on `/dev/fb1`:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb1
```

Use whichever framebuffer works in the systemd service.


## Install Display Program From GitHub

Use this after the LCD framebuffer is working.

The GitHub repository includes the current display source and a compiled binary.

Repository:

```text
https://github.com/mkinderwater/pi5-volumio-framebuffer-appliance
```

Use the `main` branch when you want the newest current copy.

### Option A: Install Current Compiled Binary

This is the fastest path.

```bash
sudo apt update
sudo apt install -y git

sudo git clone --depth 1 https://github.com/mkinderwater/pi5-volumio-framebuffer-appliance.git /opt/pi5-volumio-framebuffer-appliance

sudo install -m 755 "/opt/pi5-volumio-framebuffer-appliance/lcd program/compiled/volumio_fbd" /usr/local/bin/volumio_fbd
sudo cp "/opt/pi5-volumio-framebuffer-appliance/volumio_fbd_config.json" /etc/volumio_fbd_config.json
sudo chmod 644 /etc/volumio_fbd_config.json
```

Test it:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

If the LCD is on `/dev/fb1`:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb1
```

### Option B: Build Current Source From GitHub

Use this if you want to compile the current source on the Pi.

```bash
sudo apt update
sudo apt install -y git build-essential pkg-config libcurl4-openssl-dev libjson-c-dev libfreetype6-dev

sudo git clone --depth 1 https://github.com/mkinderwater/pi5-volumio-framebuffer-appliance.git /opt/pi5-volumio-framebuffer-appliance

cd "/opt/pi5-volumio-framebuffer-appliance/lcd program/source"

gcc -Os -pipe -Wall -Wextra -pthread \
  -ffunction-sections -fdata-sections \
  $(pkg-config --cflags freetype2) \
  volumio_fbd.c \
  -o volumio_fbd \
  $(pkg-config --libs freetype2 libcurl json-c) \
  -lm -Wl,--gc-sections -s

sudo install -m 755 volumio_fbd /usr/local/bin/volumio_fbd
sudo cp "/opt/pi5-volumio-framebuffer-appliance/volumio_fbd_config.json" /etc/volumio_fbd_config.json
sudo chmod 644 /etc/volumio_fbd_config.json
```

### Update Later From GitHub

Use this to pull the newest repo copy and reinstall the compiled binary.

```bash
sudo systemctl stop volumio-fbd 2>/dev/null || true

cd /opt/pi5-volumio-framebuffer-appliance
sudo git pull --ff-only

sudo install -m 755 "lcd program/compiled/volumio_fbd" /usr/local/bin/volumio_fbd
sudo systemctl restart volumio-fbd 2>/dev/null || true
```

If you changed `/etc/volumio_fbd_config.json`, do not overwrite it during updates unless you want the repo default config again.

## Build From Local Source

Install build dependencies:

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
sudo cp volumio_fbd /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd
```

## JSON Config

The display config lives here:

```text
/etc/volumio_fbd_config.json
```

Example:

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

If the LCD uses `/dev/fb1`, change:

```json
"fb_path": "/dev/fb0"
```

to:

```json
"fb_path": "/dev/fb1"
```

## Systemd Service

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
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true'
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

Enable and start:

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

## Do Not Add

Do not add these for this appliance:

- `startx`
- `.bash_profile` auto-start
- `lightdm`
- `raspberrypi-ui-mods`
- `chromium-browser`
- X11 touch calibration
- `fbcp`
- `/etc/rc.local` startup hacks
- MPD playback logic
- Spotify fallback artwork
- AirPlay pause guessing

Keep the truth split:

```text
Volumio tells what is playing.
ALSA tells whether audio is moving.
JSON tells how the display should look.
```
