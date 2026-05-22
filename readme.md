# Raspberry Pi 5 Volumio Framebuffer Appliance

This is a small, purpose-built Raspberry Pi 5 Volumio appliance with M.2 storage, DAC output, PoE, and a minimal LCD status screen.

The hardware is only half the project. The other half is the display program, written in C.

Instead of running a desktop, browser, kiosk mode, or heavy plugin just to show basic music info, this build writes directly to the Linux framebuffer. No X11, Wayland, Chromium, or Chromium-based UI stack runs in the background.

On my Raspberry Pi 5, the display program usually sits under 1% CPU.

The goal is simple:

Show useful Volumio information without wasting the Pi.

## Display Features

The LCD displays:

- Time
- Song title
- Artist
- Playback state
- Network status
- Offline status
- Album artwork
- Media type and sample rate
- Simple visual feedback
- Large clock mode when nothing is playing
- Smaller clock with track info when music is active
- Blinking colon as a simple heartbeat indicator

## Hardware Stack

The enclosure was designed around the actual hardware stack:

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- NVMe storage
- InnoMaker HiFi DAC Pro
- Waveshare 3.5 inch RPi LCD

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

A lot of Raspberry Pi Volumio builds are either bare boards, generic cases, or full touchscreen setups running more software than needed.

This one is built more like a simple audio appliance.

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

## As Built

- Raspberry Pi 5
- Waveshare PCIe to M.2 Adapter with PoE Function and active cooling
- 128GB NVMe M.2 storage
- InnoMaker HiFi DAC Pro
- Waveshare 3.5 inch RPi LCD

## Included Files

This repository includes:

- Source files
- OpenSCAD design files
- C display program
- Compiled binary
- Install notes
- Bill of materials
