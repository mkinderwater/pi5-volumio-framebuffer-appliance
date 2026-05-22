// Raspberry Pi 5 + PoE M.2 HAT+ + DAC + lid LCD case
// Units: mm
// Optimized final version
// 2026-05-18 patch: recessed side-use sticky-foot pockets
// 2026-05-15 adjustments: taller HDMI/USB-C and audio cutouts, wider LCD keepout for lid screws
// 2026-05-16 add-ons: NIC lower relief, DAC labels, side-use sticky-foot pads
// 2026-05-18 patch: kept known-good fit, added sharper vents, flat-top FDM wall slots, $fn=64
// 2026-05-19 patch: strengthened base lid screw bosses and added larger screw pilot lead-in
// 2026-05-19 patch: solid stepped corner lid mounts with holes, no round boss bodies
// 2026-05-19 patch: longer stronger lid mounts with deeper wall-grown stepped supports
// 2026-05-19 patch: top pilot lead-in set for 2.7 mm measured screw taper
// 2026-05-20 patch: FDM 0.20 mm layer-optimized lid mount slope and vent keepouts
// 2026-05-20 patch: trimmed lid mount reach to avoid LCD without square relief cuts; blind pilot holes

$fn = 64;

// -------------------------
// Main dimensions
// -------------------------
pi_w = 85;
pi_d = 56;
pi_th = 1.6;

lcd_w = 85.42;
lcd_d = 55.60;
lcd_window_w = 75.5;
lcd_window_d = 50.0;
lcd_pcb_clearance = 0.7;
lcd_total_h = 5.3;
lcd_z_clearance = 0.5;
lcd_pocket_h = lcd_total_h + lcd_z_clearance;

wall = 2.8;
floor_h = 5.0;
board_side_gap = 3.5;
lcd_screw_keepout = 14.0; // widened so LCD PCB clears lid screw bosses and screw heads
corner_r = 4.0;

case_w = max(pi_w + board_side_gap * 2 + wall * 2, lcd_w + lcd_screw_keepout * 2);
case_d = max(pi_d + board_side_gap * 2 + wall * 2, lcd_d + lcd_screw_keepout * 2);

inner_w = case_w - wall * 2;
inner_d = case_d - wall * 2;

pi_x = (case_w - pi_w) / 2;
pi_y = (case_d - pi_d) / 2;

// -------------------------
// M2.5 hardware
// -------------------------
m25_clearance_d = 2.9;
m25_pilot_d = 2.2;
m25_head_d = 5.6;
m25_head_recess_h = 2.4;
m25_standoff_od = 5.0;

// -------------------------
// Vertical stack
// -------------------------
base_to_pi = 8.0;
pi_to_m2 = 16.0;
m2_to_dac = 16.0;
dac_to_lcd = 15.0;

lid_h = 5.8;
lid_lip_h = 4.0;
lid_gap = 0.50;

lcd_lid_recess_depth = 2.2;

pi_z = floor_h + base_to_pi;
m2_z = pi_z + pi_th + pi_to_m2;
dac_z = m2_z + pi_th + m2_to_dac;
lcd_z = dac_z + pi_th + dac_to_lcd;

case_h = lcd_z + lcd_lid_recess_depth - 0.2;

pi_holes = [
    [3.5, 3.5],
    [61.5, 3.5],
    [3.5, 52.5],
    [61.5, 52.5]
];

// -------------------------
// Lid screws
// -------------------------
// Lid holes stay tapered for the existing screws.
// Base mounts are now solid corner-grown blocks with holes, not round bosses.
lid_screw_margin = 7.0;
lid_screw_d = 3.4;       // unchanged; lid screw pass-through
lid_screw_head_d = 6.8;  // unchanged; tapered lid recess remains as-is
lid_screw_head_recess_h = 2.8; // unchanged

// Base mount pilot holes for self-tapping lid screws.
// Main pilot gives grip. Wider entry reduces cracking at the top of the mount.
lid_mount_pilot_d = 2.45;          // main thread bite
lid_mount_pilot_entry_d = 2.85;    // clears measured 2.7 mm screw taper
lid_mount_pilot_entry_h = 2.2;     // deeper lead-in reduces top splitting
lid_mount_pilot_bottom_solid = 5.0; // leaves solid plastic at bottom of blind pilot hole

// Self-supporting base lid mount settings.
// Smooth wall-grown ramps let the slicer create the actual 0.20 mm layers.
// The lower end starts small at the corner walls, then smoothly widens into a full top pad.
// This removes stair-step geometry from the model while keeping the mount support-free on FDM.
lid_mount_top_h = 3.4;
lid_mount_grow_h = 14.4;
lid_mount_h = lid_mount_grow_h + lid_mount_top_h;
lid_mount_base_reach = 1.2;
lid_mount_wall_overlap = 0.55;
lid_mount_inner_extra = 5.0;  // trimmed so mounts stop before LCD PCB footprint, no square relief cuts
lid_mount_lip_clearance = 1.6;
lid_mount_vent_keepout = 3.0;
lid_mount_hull_slice_h = 0.25;

function lid_points() = [
    [lid_screw_margin, lid_screw_margin],
    [case_w - lid_screw_margin, lid_screw_margin],
    [lid_screw_margin, case_d - lid_screw_margin],
    [case_w - lid_screw_margin, case_d - lid_screw_margin]
];

// -------------------------
// DAC and audio
// -------------------------
dac_w = 65.0;
dac_d = 56.0;

dac_side_gap = 2.0;
dac_x = pi_x + dac_side_gap;
dac_y = pi_y;

dac_hp_x = dac_x + 14.5;
dac_bal_r_x = dac_x + 32.5;
dac_bal_l_x = dac_x + 50.5;

audio_z = dac_z + 5.6;
audio_cutout_lower = 2.0;

audio_slot_h = 17.0; // was 16.0, +1 mm taller for audio jack clearance
audio_slot_r = 3.0;
audio_slot_x = dac_x + 5.0;
audio_slot_w = 55.0;
audio_slot_bottom = audio_z - audio_slot_h / 2 - audio_cutout_lower;

// Subtle raised labels for the audio jacks.
// With the case lying on its rear wall, these sit just above the upward-facing DAC jacks.
audio_label_size = 4.2;
audio_label_raise = 0.45;
audio_label_y_embed = 0.08;
audio_label_z = min(case_h - 3.4, audio_slot_bottom + audio_slot_h + 3.2);
audio_label_font = "Liberation Sans:style=Bold";

// -------------------------
// Wall cutouts
// -------------------------
nic_usb_cutout_z = pi_z;
nic_usb_cutout_h = 20.0;
nic_usb_cutout_y = pi_y + 2 - 6 + 3;
nic_usb_cutout_d = 52 + 12 - 6;

// Lower-only NIC relief. Keeps USB opening unchanged.
network_lower_relief_y = nic_usb_cutout_y;
network_lower_relief_d = 23.0;
network_lower_relief_z = nic_usb_cutout_z - 4.0;
network_lower_relief_h = 4.2; // overlaps main opening by 0.2 mm to avoid a thin skin

right_io_avoid_y = nic_usb_cutout_y;
right_io_avoid_d = nic_usb_cutout_d;
right_io_avoid_z = network_lower_relief_z;
right_io_avoid_h = nic_usb_cutout_h + 4.0;

// Pi 5 MicroSD is opposite NIC/USB and under the PCB.
microsd_cutout_z = pi_z - 4.2;
microsd_cutout_h = 7.0;
microsd_cutout_y = pi_y + 21.0;
microsd_cutout_d = 16.0;

cutout_keepout = 10.0;

// -------------------------
// Base side vent settings
// -------------------------
fin_w = 14.0;
fin_h = 7.0;
fin_r = 1.3;

fin_pitch = 15.0;
fin_z_pitch = 8.8;

fin_wall_margin = 5.5;
fin_z_margin = 5.0;

fin_z_low = floor_h + fin_z_margin;
fin_z_high = case_h - fin_z_margin;
fin_z_space = fin_z_high - fin_z_low;

fin_rows = floor((fin_z_space - fin_h) / fin_z_pitch) + 1;
fin_grid_h = ((fin_rows - 1) * fin_z_pitch) + fin_h;
fin_z_start = fin_z_low + (fin_z_space - fin_grid_h) / 2;

fin_side_space = case_d - fin_wall_margin * 2;
fin_side_cols = floor((fin_side_space - fin_w) / fin_pitch) + 1;
fin_side_grid_len = ((fin_side_cols - 1) * fin_pitch) + fin_w;
fin_side_start = (case_d - fin_side_grid_len) / 2;

fin_rear_space = case_w - fin_wall_margin * 2;
fin_rear_cols = floor((fin_rear_space - fin_w) / fin_pitch) + 1;
fin_rear_grid_len = ((fin_rear_cols - 1) * fin_pitch) + fin_w;
fin_rear_start = (case_w - fin_rear_grid_len) / 2;

// -------------------------
// Side-use sticky foot recesses
// -------------------------
// Device sits on its rear wall with DAC/audio ports pointing up.
// These four square recessed pockets give clean adhesive-foot locations.
// Recess is shallow enough to leave rear-wall strength intact.
side_foot_w = 14.0;
side_foot_h = 14.0;
side_foot_recess_depth = 0.9;
side_foot_pocket_clearance = 0.6;
side_foot_margin_x = 11.0;
side_foot_margin_z = 7.0;
side_foot_keepout = 2.5;

function side_foot_points() = [
    [side_foot_margin_x, side_foot_margin_z],
    [case_w - side_foot_margin_x - side_foot_w, side_foot_margin_z],
    [side_foot_margin_x, case_h - side_foot_margin_z - side_foot_h],
    [case_w - side_foot_margin_x - side_foot_w, case_h - side_foot_margin_z - side_foot_h]
];

// -------------------------
// Helpers
// -------------------------
module rounded_box(size, r) {
    w = size[0];
    d = size[1];
    h = size[2];

    hull() {
        for (x = [r, w - r])
            for (y = [r, d - r])
                translate([x, y, 0])
                    cylinder(r = r, h = h);
    }
}

module rounded_slot(w, d, h, r = 2) {
    hull() {
        for (x = [r, w - r])
            for (y = [r, d - r])
                translate([x, y, 0])
                    cylinder(r = r, h = h);
    }
}

// FDM-friendly wall slot: flat bridge top with rounded lower stress relief.
module rounded_wall_slot_xz_front(w, h, depth, r = 3) {
    union() {
        translate([0, 0, r])
            cube([w, depth, h - r]);

        hull() {
            for (x = [r, w - r])
                translate([x, 0, r])
                    rotate([-90, 0, 0])
                        cylinder(r = r, h = depth);
        }
    }
}

// Correct for left wall. Cuts inward toward +X.
// FDM-friendly wall slot: flat bridge top with rounded lower stress relief.
module rounded_wall_slot_yz_left(d, h, depth, r = 2.5) {
    union() {
        translate([0, 0, r])
            cube([depth, d, h - r]);

        hull() {
            for (y = [r, d - r])
                translate([0, y, r])
                    rotate([0, 90, 0])
                        cylinder(r = r, h = depth);
        }
    }
}

function interval_clear(a0, a1, b0, b1, pad) =
    (a1 <= b0 - pad) || (a0 >= b1 + pad);

function box_clear(a0, a1, z0, z1, b0, b1, bz0, bz1, pad) =
    interval_clear(a0, a1, b0, b1, pad) ||
    interval_clear(z0, z1, bz0, bz1, pad);

function side_foot_clear(x0, x1, z0, z1) =
    min([
        for (p = side_foot_points())
            box_clear(
                x0,
                x1,
                z0,
                z1,
                p[0],
                p[0] + side_foot_w,
                p[1],
                p[1] + side_foot_h,
                side_foot_keepout
            ) ? 1 : 0
    ]) == 1;

function lid_mount_front_span() = wall + lid_mount_reach_y([lid_screw_margin, lid_screw_margin]) + lid_mount_vent_keepout;
function lid_mount_rear_start() = case_d - wall - lid_mount_reach_y([lid_screw_margin, lid_screw_margin]) - lid_mount_vent_keepout;
function lid_mount_left_span() = wall + lid_mount_reach_x([lid_screw_margin, lid_screw_margin]) + lid_mount_vent_keepout;
function lid_mount_right_start() = case_w - wall - lid_mount_reach_x([lid_screw_margin, lid_screw_margin]) - lid_mount_vent_keepout;
function lid_mount_vent_z0() = case_h - lid_mount_h - lid_mount_vent_keepout;
function lid_mount_vent_z1() = case_h + 0.5;

function side_lid_mount_vent_clear(y0, y1, z0, z1) =
    box_clear(y0, y1, z0, z1, 0, lid_mount_front_span(), lid_mount_vent_z0(), lid_mount_vent_z1(), 0) &&
    box_clear(y0, y1, z0, z1, lid_mount_rear_start(), case_d, lid_mount_vent_z0(), lid_mount_vent_z1(), 0);

function rear_lid_mount_vent_clear(x0, x1, z0, z1) =
    box_clear(x0, x1, z0, z1, 0, lid_mount_left_span(), lid_mount_vent_z0(), lid_mount_vent_z1(), 0) &&
    box_clear(x0, x1, z0, z1, lid_mount_right_start(), case_w, lid_mount_vent_z0(), lid_mount_vent_z1(), 0);

// -------------------------
// Base side vent modules
// -------------------------
module offset_fin_2d(w, h, r, flip = false) {
    // Sharp 45-degree-ish fins. The r parameter is kept for compatibility.
    slant = h * 0.6;

    if (flip) {
        polygon([
            [slant, 0],
            [w, 0],
            [w - slant, h],
            [0, h]
        ]);
    } else {
        polygon([
            [0, 0],
            [w - slant, 0],
            [w, h],
            [slant, h]
        ]);
    }
}

module fin_cut_side(xpos, ypos, zpos, flip = false) {
    translate([xpos, ypos, zpos])
        multmatrix([
            [0, 0, 1, 0],
            [1, 0, 0, 0],
            [0, 1, 0, 0],
            [0, 0, 0, 1]
        ])
            linear_extrude(height = wall + 0.9)
                offset_fin_2d(fin_w, fin_h, fin_r, flip);
}

module fin_cut_rear(xpos, ypos, zpos, flip = false) {
    translate([xpos, ypos, zpos])
        multmatrix([
            [1, 0, 0, 0],
            [0, 0, 1, 0],
            [0, 1, 0, 0],
            [0, 0, 0, 1]
        ])
            linear_extrude(height = wall + 0.9)
                offset_fin_2d(fin_w, fin_h, fin_r, flip);
}

module side_fin_grid(xpos, avoid_y, avoid_d, avoid_z, avoid_h) {
    for (row = [0 : fin_rows - 1]) {
        zpos = fin_z_start + row * fin_z_pitch;
        row_shift = (row % 2) * fin_pitch / 2;
        row_flip = (row % 2) == 1;

        for (i = [0 : fin_side_cols - 1]) {
            ypos = fin_side_start + i * fin_pitch + row_shift;

            if (
                ypos + fin_w <= case_d - fin_wall_margin &&
                box_clear(
                    ypos,
                    ypos + fin_w,
                    zpos,
                    zpos + fin_h,
                    avoid_y,
                    avoid_y + avoid_d,
                    avoid_z,
                    avoid_z + avoid_h,
                    cutout_keepout
                ) &&
                side_lid_mount_vent_clear(
                    ypos,
                    ypos + fin_w,
                    zpos,
                    zpos + fin_h
                )
            )
                fin_cut_side(xpos, ypos, zpos, row_flip);
        }
    }
}

module rear_fin_grid() {
    for (row = [0 : fin_rows - 1]) {
        zpos = fin_z_start + row * fin_z_pitch;
        row_shift = (row % 2) * fin_pitch / 2;
        row_flip = (row % 2) == 1;

        for (i = [0 : fin_rear_cols - 1]) {
            xpos = fin_rear_start + i * fin_pitch + row_shift;

            if (
                xpos + fin_w <= case_w - fin_wall_margin &&
                side_foot_clear(
                    xpos,
                    xpos + fin_w,
                    zpos,
                    zpos + fin_h
                ) &&
                rear_lid_mount_vent_clear(
                    xpos,
                    xpos + fin_w,
                    zpos,
                    zpos + fin_h
                )
            )
                fin_cut_rear(xpos, case_d - wall - 0.25, zpos, row_flip);
        }
    }
}

module fin_vent_grid() {
    side_fin_grid(
        -0.25,
        microsd_cutout_y,
        microsd_cutout_d,
        microsd_cutout_z,
        microsd_cutout_h
    );

    side_fin_grid(
        case_w - wall - 0.25,
        right_io_avoid_y,
        right_io_avoid_d,
        right_io_avoid_z,
        right_io_avoid_h
    );

    rear_fin_grid();
}

// -------------------------
// Solid lid mounts and lid relief
// -------------------------
function lid_mount_reach_x(p) =
    (p[0] < case_w / 2)
        ? (p[0] + lid_mount_inner_extra - wall)
        : (case_w - wall - (p[0] - lid_mount_inner_extra));

function lid_mount_reach_y(p) =
    (p[1] < case_d / 2)
        ? (p[1] + lid_mount_inner_extra - wall)
        : (case_d - wall - (p[1] - lid_mount_inner_extra));

module lid_corner_mount_block(p, reach_x, reach_y, zpos, block_h) {
    // Corner-anchored solid block.
    if (p[0] < case_w / 2 && p[1] < case_d / 2) {
        translate([wall - lid_mount_wall_overlap, wall - lid_mount_wall_overlap, zpos])
            cube([reach_x + lid_mount_wall_overlap, reach_y + lid_mount_wall_overlap, block_h]);
    }
    else if (p[0] >= case_w / 2 && p[1] < case_d / 2) {
        translate([case_w - wall - reach_x, wall - lid_mount_wall_overlap, zpos])
            cube([reach_x + lid_mount_wall_overlap, reach_y + lid_mount_wall_overlap, block_h]);
    }
    else if (p[0] < case_w / 2 && p[1] >= case_d / 2) {
        translate([wall - lid_mount_wall_overlap, case_d - wall - reach_y, zpos])
            cube([reach_x + lid_mount_wall_overlap, reach_y + lid_mount_wall_overlap, block_h]);
    }
    else {
        translate([case_w - wall - reach_x, case_d - wall - reach_y, zpos])
            cube([reach_x + lid_mount_wall_overlap, reach_y + lid_mount_wall_overlap, block_h]);
    }
}

module lid_corner_mount(p) {
    z0 = case_h - lid_mount_h;
    grow_h = lid_mount_grow_h;

    full_reach_x = lid_mount_reach_x(p);
    full_reach_y = lid_mount_reach_y(p);

    // Smooth lower buttress. The slicer turns this into the real FDM layer steps.
    hull() {
        lid_corner_mount_block(
            p,
            lid_mount_base_reach,
            lid_mount_base_reach,
            z0,
            lid_mount_hull_slice_h
        );

        lid_corner_mount_block(
            p,
            full_reach_x,
            full_reach_y,
            z0 + grow_h - lid_mount_hull_slice_h,
            lid_mount_hull_slice_h
        );
    }

    // Final solid square top pad. The screw hole is cut later in base_cutouts().
    lid_corner_mount_block(
        p,
        full_reach_x,
        full_reach_y,
        z0 + grow_h,
        lid_mount_top_h
    );
}

module lid_lip_corner_mount_relief(p, z0, zh) {
    // Clear the lid lip around the square wall-grown mounts.
    // This replaces the old circular boss relief.
    reach_x = lid_mount_reach_x(p) + lid_mount_lip_clearance;
    reach_y = lid_mount_reach_y(p) + lid_mount_lip_clearance;

    if (p[0] < case_w / 2 && p[1] < case_d / 2) {
        translate([wall - 1.2, wall - 1.2, z0])
            cube([reach_x + 2.4, reach_y + 2.4, zh]);
    }
    else if (p[0] >= case_w / 2 && p[1] < case_d / 2) {
        translate([case_w - wall - reach_x - 1.2, wall - 1.2, z0])
            cube([reach_x + 2.4, reach_y + 2.4, zh]);
    }
    else if (p[0] < case_w / 2 && p[1] >= case_d / 2) {
        translate([wall - 1.2, case_d - wall - reach_y - 1.2, z0])
            cube([reach_x + 2.4, reach_y + 2.4, zh]);
    }
    else {
        translate([case_w - wall - reach_x - 1.2, case_d - wall - reach_y - 1.2, z0])
            cube([reach_x + 2.4, reach_y + 2.4, zh]);
    }
}

module lid_lip_boss_relief() {
    z0 = -lid_lip_h - 0.6;
    zh = lid_lip_h + 1.2;

    for (p = lid_points())
        lid_lip_corner_mount_relief(p, z0, zh);
}

module side_sticky_foot_recesses() {
    for (p = side_foot_points()) {
        translate([
            p[0] - side_foot_pocket_clearance / 2,
            case_d - side_foot_recess_depth,
            p[1] - side_foot_pocket_clearance / 2
        ])
            cube([
                side_foot_w + side_foot_pocket_clearance,
                side_foot_recess_depth + 0.15,
                side_foot_h + side_foot_pocket_clearance
            ]);
    }
}

module front_audio_label(txt, xpos) {
    translate([xpos, audio_label_y_embed, audio_label_z])
        rotate([90, 0, 0])
            linear_extrude(height = audio_label_raise + audio_label_y_embed)
                text(
                    txt,
                    size = audio_label_size,
                    font = audio_label_font,
                    halign = "center",
                    valign = "center"
                );
}

module front_audio_labels() {
    // Physical order at the DAC opening is headphone, right, left.
    front_audio_label("H", dac_hp_x);
    front_audio_label("R", dac_bal_r_x);
    front_audio_label("L", dac_bal_l_x);
}

// -------------------------
// Base cutouts
// -------------------------
module base_cutouts() {
    // NIC / USB side.
    translate([case_w - wall - 0.2, nic_usb_cutout_y, nic_usb_cutout_z])
        cube([wall + 0.8, nic_usb_cutout_d, nic_usb_cutout_h]);

    // Lower-only NIC relief. USB opening remains unchanged.
    translate([case_w - wall - 0.2, network_lower_relief_y, network_lower_relief_z])
        cube([wall + 0.8, network_lower_relief_d, network_lower_relief_h]);

    // USB-C / HDMI side.
    translate([pi_x + 4, -0.2, pi_z - 4])
        rounded_wall_slot_xz_front(42, 14, wall + 0.8, 2.2);

    // MicroSD side, opposite NIC / USB.
    translate([-0.1, microsd_cutout_y, microsd_cutout_z])
        rounded_wall_slot_yz_left(
            microsd_cutout_d,
            microsd_cutout_h,
            wall + 1.0,
            2.0
        );

    // Audio jacks.
    translate([audio_slot_x, -0.7, audio_slot_bottom])
        rounded_wall_slot_xz_front(
            audio_slot_w,
            audio_slot_h,
            wall + 1.6,
            audio_slot_r
        );

    // Bottom intake.
    for (x = [16 : 12 : case_w - 20])
        translate([x, case_d / 2 - 17, -0.1])
            rounded_slot(5, 34, floor_h + 0.3, 2);

    // Wall vents.
    fin_vent_grid();

    // Recessed rear-wall pockets for adhesive feet.
    side_sticky_foot_recesses();

    // Bottom M2.5 screw access for Pi standoffs.
    for (h = pi_holes) {
        translate([pi_x + h[0], pi_y + h[1], -0.2])
            cylinder(d = m25_clearance_d, h = floor_h + 0.6);

        translate([pi_x + h[0], pi_y + h[1], -0.2])
            cylinder(d = m25_head_d, h = m25_head_recess_h + 0.2);
    }

    // Blind lid screw pilot holes in solid corner mounts.
    // The pilot runs deep, but does not break through the bottom of the mount.
    // Wider top entry helps the screw start cleanly and reduces cracking.
    lid_mount_pilot_depth = lid_mount_h - lid_mount_pilot_bottom_solid;

    for (p = lid_points()) {
        translate([p[0], p[1], case_h - lid_mount_pilot_depth])
            cylinder(d = lid_mount_pilot_d, h = lid_mount_pilot_depth + 0.3);

        translate([p[0], p[1], case_h - lid_mount_pilot_entry_h])
            cylinder(d = lid_mount_pilot_entry_d, h = lid_mount_pilot_entry_h + 0.3);
    }
}

// -------------------------
// Base
// -------------------------
module base() {
    difference() {
        union() {
            difference() {
                rounded_box([case_w, case_d, case_h], corner_r);

                translate([wall, wall, floor_h])
                    rounded_box(
                        [inner_w, inner_d, case_h + 0.5],
                        max(1.5, corner_r - wall)
                    );
            }

            for (p = lid_points())
                lid_corner_mount(p);

            front_audio_labels();
        }

        base_cutouts();
    }
}

// -------------------------
// Lid
// -------------------------
module lid() {
    lcd_x = (case_w - lcd_w) / 2;
    lcd_y = (case_d - lcd_d) / 2;

    win_x = (case_w - lcd_window_w) / 2;
    win_y = (case_d - lcd_window_d) / 2;

    difference() {
        union() {
            rounded_box([case_w, case_d, lid_h], corner_r);

            translate([wall + lid_gap, wall + lid_gap, -lid_lip_h + 0.1])
            difference() {
                rounded_box(
                    [inner_w - lid_gap * 2, inner_d - lid_gap * 2, lid_lip_h],
                    max(1.5, corner_r - wall)
                );

                translate([2, 2, -0.1])
                    rounded_box(
                        [inner_w - lid_gap * 2 - 4, inner_d - lid_gap * 2 - 4, lid_lip_h + 0.2],
                        2
                    );
            }
        }

        // Clear lid lip around base screw bosses.
        lid_lip_boss_relief();

        // LCD viewing window.
        translate([win_x, win_y, -0.2])
            rounded_slot(lcd_window_w, lcd_window_d, lid_h + 0.5, 2);

        // LCD underside pocket for 5.3 mm LCD stack.
        translate([
            lcd_x - lcd_pcb_clearance,
            lcd_y - lcd_pcb_clearance,
            -lcd_lid_recess_depth - 0.1
        ])
            cube([
                lcd_w + lcd_pcb_clearance * 2,
                lcd_d + lcd_pcb_clearance * 2,
                lcd_pocket_h + 0.2
            ]);

        // LCD header relief.
        translate([lcd_x + 24, lcd_y + lcd_d - 11, -lid_lip_h - 0.2])
            cube([58, 12, lid_lip_h + 1.0]);

        // Lid screw holes and countersinks.
        for (p = lid_points()) {
            translate([p[0], p[1], -0.2])
                cylinder(d = lid_screw_d, h = lid_h + 0.5);

            translate([p[0], p[1], lid_h - lid_screw_head_recess_h])
                cylinder(
                    d1 = lid_screw_d,
                    d2 = lid_screw_head_d,
                    h = lid_screw_head_recess_h + 0.2
                );
        }
    }
}

module lid_print() {
    translate([0, 0, lid_h])
        rotate([180, 0, 0])
            lid();
}

// -------------------------
// Preview helpers
// -------------------------
module stack_preview() {
    color([0, 0.5, 0, 0.35])
        translate([pi_x, pi_y, pi_z])
            cube([pi_w, pi_d, pi_th]);

    color([0, 0, 0.8, 0.35])
        translate([pi_x, pi_y, m2_z])
            cube([70, 56.5, pi_th]);

    color([0, 0.5, 0, 0.35])
        translate([dac_x, dac_y, dac_z])
            cube([dac_w, dac_d, pi_th]);

    color([0.1, 0.1, 0.1, 0.35])
        translate([(case_w - lcd_w) / 2, (case_d - lcd_d) / 2, lcd_z])
            cube([lcd_w, lcd_d, lcd_total_h]);

    color([1, 0.2, 0.2, 0.45]) {
        translate([dac_hp_x, 1, audio_z])
            rotate([-90, 0, 0])
                cylinder(d = 4, h = 4);

        translate([dac_bal_r_x, 1, audio_z])
            rotate([-90, 0, 0])
                cylinder(d = 4, h = 4);

        translate([dac_bal_l_x, 1, audio_z])
            rotate([-90, 0, 0])
                cylinder(d = 4, h = 4);
    }

    color([0.8, 0.55, 0.15, 0.45])
        for (h = pi_holes)
            translate([pi_x + h[0], pi_y + h[1], floor_h])
                cylinder(d = m25_standoff_od, h = base_to_pi);

    color([0.8, 0.55, 0.15, 0.35])
        for (h = pi_holes)
            translate([pi_x + h[0], pi_y + h[1], pi_z + pi_th])
                cylinder(d = m25_standoff_od, h = pi_to_m2);

    color([0.8, 0.55, 0.15, 0.35])
        for (h = pi_holes)
            translate([pi_x + h[0], pi_y + h[1], m2_z + pi_th])
                cylinder(d = m25_standoff_od, h = m2_to_dac);

    color([0.8, 0.55, 0.15, 0.35])
        for (h = pi_holes)
            translate([pi_x + h[0], pi_y + h[1], dac_z + pi_th])
                cylinder(d = m25_standoff_od, h = dac_to_lcd);
}

// -------------------------
// Export selector
// -------------------------
part = "base";       // smooth self-supporting solid lid mounts
// part = "lid";
// part = "both";
// part = "assembled";

if (part == "base") {
    base();
}
else if (part == "lid") {
    lid_print();
}
else if (part == "both") {
    base();

    translate([0, case_d + 14, 0])
        lid_print();
}
else if (part == "assembled") {
    base();
    stack_preview();

    translate([0, 0, case_h + 0.2])
        lid();
}