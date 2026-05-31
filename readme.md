# Raspberry Pi 5 Volumio Framebuffer Appliance

Minimal setup for a Raspberry Pi 5 Volumio player with a 3.5 inch GPIO LCD.

This assumes a fresh bare-metal Volumio install.

No desktop.  
No browser.  
No X11.  
No vendor LCD installer.

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
sudo apt install -y git
sudo mkdir -p /boot/overlays

cd /tmp
git clone --depth 1 https://github.com/goodtft/LCD-show.git

sudo cp /tmp/LCD-show/usr/tft35a-overlay.dtb /boot/overlays/tft35a.dtbo
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

## 2. Verify Framebuffer

After reboot:

```bash
ls -l /dev/fb*
cat /sys/class/graphics/fb0/name
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

## 3. Install Display Program From GitHub

Repository:

```text
https://github.com/mkinderwater/pi5-volumio-framebuffer-appliance
```

Install the compiled binary:

```bash
sudo apt update
sudo apt install -y git

sudo git clone --depth 1 https://github.com/mkinderwater/pi5-volumio-framebuffer-appliance.git /opt/pi5-volumio-framebuffer-appliance

sudo cp "/opt/pi5-volumio-framebuffer-appliance/lcd program/compiled/volumio_fbd" /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd
```

Run it once:

```bash
sudo /usr/local/bin/volumio_fbd /dev/fb0
```

The first run creates the config file:

```text
/etc/volumio_fbd_config.json
```

Edit it after first run:

```bash
sudo nano /etc/volumio_fbd_config.json
```

If the LCD uses `/dev/fb1`, change the config value from:

```json
"fb_path": "/dev/fb0"
```

to:

```json
"fb_path": "/dev/fb1"
```

## 4. Optional: Build From Source

Use this only if you want to compile it on the Pi.

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

sudo cp volumio_fbd /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd
```

Run once to create the config:

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

## 6. Update Later

```bash
sudo systemctl stop volumio-fbd 2>/dev/null || true

cd /opt/pi5-volumio-framebuffer-appliance
sudo git pull --ff-only

sudo cp "lcd program/compiled/volumio_fbd" /usr/local/bin/volumio_fbd
sudo chmod 755 /usr/local/bin/volumio_fbd

sudo systemctl restart volumio-fbd
```

This does not overwrite your JSON config.
