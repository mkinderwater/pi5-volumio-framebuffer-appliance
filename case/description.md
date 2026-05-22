# Raspberry Pi 5 Volumio Appliance Enclosure

This project is a custom Raspberry Pi 5 Volumio appliance build with a stacked internal layout and a 3D-printed enclosure.

## Hardware Stack

The stack includes:

- Raspberry Pi 5
- PoE / M.2 HAT+
- DAC board
- LCD screen in the lid
- Front cutouts for USB-C, HDMI, and audio jacks
- Side cutouts for NIC and USB
- Bottom intake vents
- Stylized offset fin vents on the side walls
- M2.5 hardware for board mounting
- Self-tapping screws for securing the lid

## Enclosure Design

The enclosure is designed in OpenSCAD and is built around exact board spacing:

- Pi to M.2 HAT+
- M.2 HAT+ to DAC
- DAC to LCD

The base is strengthened for the M2.5 standoffs.

The lid includes:

- LCD window
- Underside LCD pocket
- Header relief
- Screw holes
- Countersinks

## Latest Revision Notes

The latest revision includes these fit improvements:

- Increased the USB-C / HDMI cutout height by 2 mm
- Increased the audio jack cutout height by 1 mm
- Moved the lid screw supports farther from the LCD
- Increased the LCD keepout area

These changes prevent the LCD PCB from sitting on the screw supports and give the screen more mechanical clearance.

## OpenSCAD Export Selector

The OpenSCAD file should include a part selector near the top of the file.

This lets you export only the part you want.

```scad
// =====================================================
// EXPORT SELECTOR
// Options:
// "base"      = print only the lower enclosure
// "lid"       = print only the top lid
// "assembled" = show the complete enclosure for checking fit
// =====================================================

export_part = "assembled";
```

## Export Options

Use these values when exporting parts from OpenSCAD:

| Value | Use |
|---|---|
| `base` | Export only the lower enclosure |
| `lid` | Export only the top lid |
| `assembled` | Show the complete enclosure for fit checking |

Do not print the assembled preview as a final part.
