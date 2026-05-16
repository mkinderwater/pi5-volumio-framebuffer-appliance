This is a small, purpose-built Raspberry Pi 5 Volumio appliance with M.2 storage, DAC output, PoE, and a minimal LCD status screen.

The hardware is only half the project. The other half is the display program, written in C.

Instead of running a desktop, browser, kiosk mode, or heavy plugin just to show basic music info, this build writes directly to the Linux framebuffer.

- No X11, Wayland, Chromium, or bloated UI stack sitting in the background.

The goal is simple: show useful Volumio information without wasting the Pi.

The screen displays:

- Time
- Song title
- Artist
- Playback state
- Network status
- Offline status
- Simple visual feedback
- Large clock mode when nothing is playing
- Smaller clock with track info when music is active
- Blinking colon as a simple heartbeat indicator

The enclosure was designed around the actual hardware stack:

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- NVMe storage
- InnoMaker HiFi DAC Pro
- Waveshare 3.5 inch RPi LCD

The OpenSCAD case includes cutouts for USB-C, HDMI, audio jacks, Ethernet, USB, and MicroSD. It also includes bottom intake vents, side fin vents, stronger M2.5 mounting points, lid screws, DAC jack labels, and solid sticky-foot pads for side orientation. A lot of Raspberry Pi Volumio builds are either bare boards, generic cases, or full touchscreen setups running more software than needed. This one is built more like a simple audio appliance.

It is not trying to be a commercial hi-fi unit. It is a clean Raspberry Pi 5 Volumio player with M.2 NVMe storage, DAC output, PoE support, and a minimal LCD display that does one job well.

As built:

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function, active cooling
- 128GB NVMe M.2 storage
- InnoMaker HiFi DAC Pro
- Waveshare 3.5 inch RPi LCD

Source files, OpenSCAD design files, the C display program, compiled binary, install notes, and BOM are included.