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

The first run creates:

```text
/etc/volumio_fbd_config.json
```

Edit it here:

```bash
sudo nano /etc/volumio_fbd_config.json
```

Common changes:

### Framebuffer

Use `/dev/fb0` unless your LCD works on `/dev/fb1`.

```json
"fb_path": "/dev/fb0"
```

or:

```json
"fb_path": "/dev/fb1"
```

### Volumio API URL

Default local Volumio status endpoint:

```json
"volumio_url": "http://127.0.0.1:3000/api/v1/getState"
```

### Album Art

Album art can be enabled or disabled:

```json
"show_album_art": true
```

or:

```json
"show_album_art": false
```

### Screen Brightness

Set the LCD brightness level used by the display program:

```json
"brightness": 100
```

### Clock Mode

Large clock mode is used when nothing is playing.

```json
"large_clock_when_idle": true
```

or:

```json
"large_clock_when_idle": false
```

### Restart After Changes

After editing the file:

```bash
sudo systemctl restart volumio-fbd
```
