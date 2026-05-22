# volumio_fbd

This project is a lightweight display interface for a Raspberry Pi running Volumio.

It does not use Chromium, X11, Wayland, SDL, Qt, or the official Volumio touch display plugin. It writes directly to the Linux framebuffer at `/dev/fb0`.

The goal is simple: show useful Volumio playback information with very low overhead.

## What it displays when idle

- Large local clock
- Blinking colon heartbeat
- Current date
- Local IP address
- CPU temperature
- Clean idle screen when nothing is playing

## What it displays during playback

- Smaller clock
- Song title
- Artist
- Album art, when available
- Playback progress bar
- Elapsed and total track time
- Playback state
- Media info, when available

When playback stops or nothing is playing, the display returns to the large clock.

## Time source

The clock does not get time from Volumio.

It reads the Linux system clock using `time(NULL)`, `localtime_r()`, and `strftime()`.

If the OS clock is synced by NTP, the display follows automatically.

## Volumio data source

Playback data comes from the local Volumio API:

```text
http://127.0.0.1:3000/api/v1/getState
```

The program polls the API, parses the JSON response with json-c, and updates the screen based on whether Volumio is playing, paused, stopped, or unavailable.

## Graphics

Graphics are rendered in C.

Fonts are rendered with FreeType, converted to RGB565, and copied directly into the framebuffer.

This avoids a browser-based UI stack and keeps CPU usage low.

## Config file

The config file is located at:

```text
/etc/volumio_display.json
```

On startup, the program can create the config file automatically if it does not already exist.

The service runs as root so it can access `/dev/fb0` and create the config file under `/etc`.

# Install instructions

## 1. Enable SSH on Volumio

Open this in a browser:

```text
http://YOUR-VOLUMIO-IP/dev
```

Enable SSH.

## 2. Login to Volumio

```bash
ssh volumio@volumio.local
```

Default Volumio login is usually:

```text
user: volumio
password: volumio
```

## 3. Install the LCD framebuffer overlay

Create the overlay directory:

```bash
sudo mkdir -p /boot/overlays
```

Download the GoodTFT 3.5 inch SPI LCD overlay:

```bash
sudo curl -L https://github.com/goodtft/LCD-show/raw/master/usr/tft35a-overlay.dtb -o /boot/overlays/tft35a.dtbo
```

## 4. Configure the framebuffer LCD

Edit the Volumio user config:

```bash
sudo nano /boot/userconfig.txt
```

Add:

```text
# GoodTFT 3.5 inch SPI LCD framebuffer
dtparam=spi=on
dtoverlay=tft35a:rotate=270
```

Save and exit.

## 5. Reboot and confirm framebuffer exists

```bash
sudo reboot
```

After reboot:

```bash
ssh volumio@volumio.local
ls -l /dev/fb*
```

You should see:

```text
/dev/fb0
```

## 6. Upload the binary

Upload the compiled binary to:

```text
/home/volumio/volumio_fbd
```

Example using SCP from another machine:

```bash
scp volumio_fbd volumio@volumio.local:/home/volumio/
```

## 7. Install the binary

```bash
sudo cp /home/volumio/volumio_fbd /usr/local/bin/volumio_fbd
sudo chmod +x /usr/local/bin/volumio_fbd
```

## 8. Create the systemd service

```bash
sudo nano /etc/systemd/system/volumio_fbd.service
```

Paste:

```ini
[Unit]
Description=Volumio framebuffer display
After=local-fs.target systemd-udev-settle.service network-online.target time-sync.target volumio.service
Wants=systemd-udev-settle.service network-online.target time-sync.target

[Service]
Type=simple
ExecStartPre=/bin/sh -c 'for i in $(seq 1 30); do [ -e /dev/fb0 ] && exit 0; sleep 1; done; echo "/dev/fb0 missing"; exit 1'
ExecStart=/usr/local/bin/volumio_fbd
Restart=always
RestartSec=2
User=root

[Install]
WantedBy=multi-user.target
```

Save and exit.

## 9. Enable and start the service

```bash
sudo systemctl daemon-reload
sudo systemctl enable volumio_fbd.service
sudo systemctl start volumio_fbd.service
```

## 10. Check service status

```bash
systemctl status volumio_fbd.service --no-pager
```

View logs:

```bash
journalctl -u volumio_fbd.service -n 80 --no-pager
```

## 11. Reboot test

```bash
sudo reboot
```

After reboot, the display should start automatically.

# Notes

This service runs as root because it needs direct framebuffer access.

This setup assumes the LCD is exposed as `/dev/fb0`.

Do not install the official Volumio touch display plugin for this setup. This project replaces that type of display stack with a direct framebuffer program.
