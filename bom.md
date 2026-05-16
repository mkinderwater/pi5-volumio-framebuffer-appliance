# Raspberry Pi 5 Volumio Network Streamer BOM

Project: Raspberry Pi 5 Volumio case with PoE, M.2 NVMe, DAC, LCD, NIC relief, H/R/L DAC labels, and side sticky-foot pads.

Base design file: `case-base-fits-perfect-nic-labels-feet.scad`

The working base is the fit-good 2026-05-15 SCAD geometry with these add-ons retained:

- NIC lower relief cutout
- Subtle raised DAC jack labels: `H`, `R`, `L`
- Four square solid sticky-foot pads for side use
- DAC/audio ports face up when the unit is lying on its side

## 1. Printed parts

| Qty | Part | Details | Notes |
|---:|---|---|---|
| 1 | Base shell | Export from SCAD with `part = "base"` | Includes Pi cavity, vents, NIC/USB cutouts, audio cutout, sticky-foot pads, and DAC labels |
| 1 | Lid | Export from SCAD with `part = "lid"` | Includes LCD window, underside LCD pocket, screw holes, and lid lip |
| 1 | Optional assembled preview | Export with `part = "assembled"` only for checking | Do not print assembled preview as a final part |

Recommended print material:

- PETG preferred for heat tolerance and durability
- PLA+ acceptable for light-duty indoor use
- ABS/ASA acceptable only if your printer is tuned for it

Suggested print setup:

- Layer height: 0.20 mm
- Walls/perimeters: 4
- Top/bottom layers: 5 or more
- Infill: 20% to 35%
- Supports: only where your slicer truly needs them
- Print orientation: base flat on bottom, lid face down or as already validated

Approximate printed size from the current SCAD:

| Measurement | Approx. value |
|---|---:|
| Case width | 113.42 mm |
| Case depth | 83.60 mm |
| Base height | 66.80 mm |
| Lid height | 5.80 mm |
| Assembled height | about 72.80 mm |
| Sticky-foot pad area | 14 mm x 14 mm each |

## 2. Core electronics

| Qty | Part | Suggested spec | Notes |
|---:|---|---|---|
| 1 | Raspberry Pi 5 | 4 GB or 8 GB | 8 GB preferred, but Volumio itself does not need much RAM |
| 1 | Raspberry Pi 5 compatible PoE M.2 HAT+ | PoE plus NVMe support | Must physically match your tested stack height |
| 1 | M.2 NVMe SSD | 2230, 2242, or 2280 depending on the HAT | Use the size supported by your HAT |
| 1 | DAC board / DAC HAT | Same DAC used for the fit test | Case labels assume jacks are ordered `H`, `R`, `L` from left to right on the front audio side |
| 1 | 3.5 inch SPI LCD | 480 x 320 class LCD using the `tft35a` style overlay | Current lid geometry is based on `lcd_w = 85.42 mm`, `lcd_d = 55.60 mm` |
| 1 | MicroSD card | 16 GB minimum, 32 GB or larger preferred | Used for Volumio install unless booting fully from NVMe |
| 1 | PoE switch or PoE injector | Must match the PoE HAT requirement | Required if powering by Ethernet |
| 1 | Ethernet cable | Cat5e or Cat6 | Use a short clean cable for the final desktop build |

## 3. Display and control items

| Qty | Part | Details | Notes |
|---:|---|---|---|
| 1 | LCD overlay file | `tft35a.dtbo` | Used for framebuffer LCD support |
| 1 | LCD framebuffer display program | `volumio_clock_fb` binary or compiled source | Runs the lean Volumio clock/status UI |
| 1 | Touchscreen input device | ADS7846 style touchscreen, if your LCD has touch | Optional if using tap-to-play/pause |
| 1 | Font package | DejaVu or Liberation fonts | Needed by the C framebuffer UI if rendering text with FreeType |

## 4. Fasteners and standoffs

The SCAD stack spacing is:

| Stack layer | Spacing |
|---|---:|
| Case floor to Pi | 8 mm |
| Pi to M.2 HAT | 16 mm |
| M.2 HAT to DAC | 16 mm |
| DAC to LCD | 15 mm |

Recommended hardware:

| Qty | Part | Suggested size | Use |
|---:|---|---|---|
| 4 | Bottom screws | M2.5 x 6 mm, pan head or socket head | Through bottom of case into first standoff |
| 4 | First standoff set | M2.5, 8 mm | Case floor to Raspberry Pi |
| 4 | Second standoff set | M2.5, 16 mm | Raspberry Pi to M.2 HAT |
| 4 | Third standoff set | M2.5, 16 mm | M.2 HAT to DAC board |
| 4 | Fourth standoff set | M2.5, 15 mm | DAC board to LCD mounting layer |
| 12 to 20 | Board screws | M2.5 x 4 mm to M2.5 x 6 mm | Securing boards to standoffs |
| 4 | Lid screws | M2.5 self-tapping or M2.5 machine screws suited to the printed pilot | Securing lid to base bosses |
| 1 | NVMe screw | Usually M2 x 3 mm | Often included with the M.2 HAT |
| 4 | Sticky rubber feet | Square, about 12 mm to 14 mm | Stick to the four solid pads on the rear side of the case |

Notes:

- The SCAD uses M2.5-style board hardware.
- Lid pilot diameter is modeled at 2.2 mm.
- Bottom screw access holes are modeled for M2.5 clearance and head recess.
- Use low-profile heads where space is tight.
- Do not force screws into printed plastic. Start them straight.

## 5. Audio parts

| Qty | Part | Details | Notes |
|---:|---|---|---|
| 1 | Headphone cable or plug | Matches the DAC headphone jack | Case label: `H` |
| 1 | Right output cable | Matches the DAC right output jack | Case label: `R` |
| 1 | Left output cable | Matches the DAC left output jack | Case label: `L` |
| 1 | Amplifier, powered speakers, or headphone amp | As required | Depends on how the streamer is being used |

The labels are intentionally subtle and raised. They are only meant to avoid relying on PCB silkscreening once the board is inside the case.

## 6. Software and setup materials

| Qty | Item | Details | Notes |
|---:|---|---|---|
| 1 | Volumio image | Clean Volumio install for Raspberry Pi | Flash to microSD or NVMe boot media |
| 1 | SSH client | Windows Terminal, PuTTY, or Linux/macOS terminal | Used for setup |
| 1 | File copy method | SCP, SFTP, or USB transfer | Used to place the display binary on the Pi |
| 1 | systemd service | `volumio-clock.service` | Starts the framebuffer display at boot |
| 1 | Framebuffer program | `volumio_clock_fb` | Copy to `/usr/local/bin/` |

If compiling the C version on Volumio, expected packages include:

- `gcc`
- `pkg-config`
- `libfreetype6-dev`
- `libjson-c-dev`
- `fonts-dejavu-core` or equivalent fonts

## 7. Tools

| Qty | Tool | Use |
|---:|---|---|
| 1 | 3D printer | Print base and lid |
| 1 | Slicer | Generate G-code |
| 1 | Digital calipers | Verify LCD, jack, and board spacing |
| 1 | Small Phillips or hex driver set | M2/M2.5 hardware |
| 1 | Deburring tool or hobby knife | Clean cutouts |
| 1 | Small file set | Tune tight print areas |
| 1 | Tweezers or needle nose pliers | Handling small screws |
| 1 | Isopropyl alcohol wipe | Clean foot-pad area before applying sticky feet |

## 8. Consumables

| Qty | Item | Details |
|---:|---|---|
| 150 g to 250 g | Filament | Estimate depends on slicer settings |
| 4 | Sticky feet | Square rubber or silicone pads |
| As needed | M2.5 screws | Keep spares |
| As needed | M2.5 standoffs | Keep spare 5 mm, 8 mm, 10 mm, 15 mm, and 16 mm sizes |
| As needed | Small zip ties or cable clips | Only if internal wiring is added |

## 9. Fit-critical notes

Do not replace the current lid/LCD geometry with v4.9 geometry. The current base is the version that fits properly.

Critical SCAD values:

```scad
lcd_w = 85.42;
lcd_d = 55.60;
lcd_screw_keepout = 14.0;
audio_slot_h = 17.0;
side_foot_w = 14.0;
side_foot_h = 14.0;
side_foot_raise = 1.0;
```

The case is intended to sit on its rear side with the DAC/audio ports facing up. The four square solid pads are for adhesive feet so the case does not slide on a desk.

## 10. Suggested spare parts

Keep these on hand:

- Extra M2.5 x 4 mm screws
- Extra M2.5 x 6 mm screws
- Extra M2.5 8 mm, 15 mm, and 16 mm standoffs
- Extra sticky feet
- Spare microSD card
- Spare short Ethernet cable
- Spare LCD ribbon/header parts, if your display uses them
- Small washers for minor stack-height correction

