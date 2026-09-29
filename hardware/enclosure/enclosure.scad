// hue-simple-switch mains carrier: printable enclosure (OpenSCAD 2021+).
//
// Two parts, printed flat side down, no supports:
//   base - the deep part under the board: power module, terminals, fuse.
//          Holds the board on posts. Wire holes in the side wall, screw
//          access holes in the floor.
//   lid  - the shallow cap over the XIAO. USB-C cut-out, window over the
//          BOOT/RESET buttons and the LED.
// The lid's skirt slides over a step at the top of the base wall and clicks
// onto two ridges on the straight sides.
//
// Render one part:  openscad -o base.stl -D 'part="base"' enclosure.scad
// Coordinates are the PCB's (mm from the board centre, x right, y DOWN as in
// KiCad); the model is mirrored at the end so it matches the real board seen
// from the XIAO side. z = 0 is the outside of the base floor.
//
// Print in a flame-retardant or at least heat-resistant plastic (PC, ASA,
// PETG; ideally a V-0 rated grade). Not PLA: it creeps when warm.

part = "both";          // "base", "lid", "both" (assembled view), "print" (both, side by side)

/* [Board] */
board_r      = 26.0;    // round ends
board_half_w = 21.0;    // straight sides at x = +-21
board_t      = 1.6;

/* [Enclosure] */
gap        = 0.4;       // board edge to inner wall
wall       = 1.6;
floor_t    = 1.2;
roof_t     = 1.2;
below      = 16.0;      // floor to board underside (power module is 15 mm)
above      = 6.0;       // board top to roof inside (XIAO + USB-C is ~4.5 mm)
step_h     = 2.4;       // height of the lid/base overlap
fit        = 0.15;      // clearance of the sliding fit, per side
post_d     = 2.6;

$fn = 64;

// derived
in_r   = board_r + gap;
in_w   = board_half_w + gap;
out_r  = in_r + wall;
out_w  = in_w + wall;
z_bb   = floor_t + below;          // board bottom
z_bt   = z_bb + board_t;           // board top
z_top  = z_bt + above + roof_t;    // outside of the roof
z_join = z_bt;                     // where the lid meets the base

echo(str("Outside: ", 2 * out_w, " x ", 2 * out_r, " mm, height ", z_top, " mm"));

// ---------------------------------------------------------------- features
// Wire entries (x or y of the terminal pins, from the PCB), z below the board.
j1_pins = [-12.75, -7.67];            // L, N at the right wall
j2_pins = [1.0, 4.5, 8.0, 11.5];      // D0..D3 at the right wall
j3_pins = [11.4, 7.9, 4.4];           // D4, D5, GND at the bottom (y+) wall
j1_hole = 3.6;  j1_z = z_bb - 3.9;
jl_hole = 3.0;  jl_z = z_bb - 3.0;

// Screw access in the floor: [x, y, length along the entry direction, width]
screws = concat(
    [for (y = j1_pins) [17.4, y, 5.0, 4.2, 0]],
    [for (y = j2_pins) [17.4, y, 4.0, 3.4, 0]],
    [for (x = j3_pins) [x, 19.0, 4.0, 3.4, 90]]);

// Board supports, bottom side (free spots on the PCB)
base_posts = [[-14.0, -21.0], [-4.0, -22.0], [-8.0, 21.8], [19.6, -2.9], [16.5, 17.8]];
// Hold-downs from the roof, top side
lid_posts  = [[14.6, -2.0], [1.0, -9.0], [-10.0, 21.8], [8.0, 21.6]];

// USB-C on the XIAO (left edge) and the button/LED window
usb_y = 2.8;  usb_w = 12.6;  usb_h = 7.2;  usb_zc = z_bt + 1.0 + 1.6;
win   = [[-20.4, 2.8 - 7.5], [-15.2, 2.8 + 7.5]];   // x0,y0 .. x1,y1

// ---------------------------------------------------------------- shapes
module outline2d(r, w) { intersection() { circle(r = r); square([2 * w, 2 * r + 2], center = true); } }

module shell(z0, z1, r_out, w_out, r_in, w_in) {
    difference() {
        translate([0, 0, z0]) linear_extrude(z1 - z0) outline2d(r_out, w_out);
        translate([0, 0, z0 - 1]) linear_extrude(z1 - z0 + 2) outline2d(r_in, w_in);
    }
}

module wire_holes() {
    for (y = j1_pins) translate([in_w - 1, y, j1_z]) rotate([0, 90, 0]) cylinder(d = j1_hole, h = wall + 3);
    for (y = j2_pins) translate([in_w - 1, y, jl_z]) rotate([0, 90, 0]) cylinder(d = jl_hole, h = wall + 3);
    for (x = j3_pins) translate([x, 0, jl_z]) rotate([-90, 0, 0]) cylinder(d = jl_hole, h = out_r + 2);
}

module usb_cut() {
    translate([-out_w - 1, usb_y - usb_w / 2, usb_zc - usb_h / 2])
        cube([wall + gap + 2, usb_w, usb_h]);
}

// ---------------------------------------------------------------- base
module base() {
    difference() {
        union() {
            // floor
            linear_extrude(floor_t) outline2d(out_r, out_w);
            // wall, with the outer step at the top for the lid skirt
            shell(0, z_join - step_h, out_r, out_w, in_r, in_w);
            shell(z_join - step_h, z_join, out_r - wall / 2 - fit, out_w - wall / 2 - fit, in_r, in_w);
            // click ridges on the straight sides
            for (s = [-1, 1]) translate([s * (out_w - wall / 2 - fit), 0, z_join - step_h / 2])
                rotate([90, 0, 0]) cylinder(d = 0.9, h = 16, center = true, $fn = 16);
            // board supports
            for (p = base_posts) translate([p[0], p[1], 0]) cylinder(d = post_d, h = z_bb);
        }
        wire_holes();
        usb_cut();
        for (s = screws) translate([s[0], s[1], -1]) rotate([0, 0, s[4]])
            hull() for (d = [-1, 1]) translate([d * (s[2] - s[3]) / 2, 0, 0]) cylinder(d = s[3], h = floor_t + 2);
        // "MAINS" mark on the floor (reads correctly from outside, below)
        translate([-6, -10, -0.01]) linear_extrude(0.4)
            text("MAINS 100-240V~", size = 2.6, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
}

// ---------------------------------------------------------------- lid
module lid() {
    difference() {
        union() {
            // roof
            translate([0, 0, z_top - roof_t]) linear_extrude(roof_t) outline2d(out_r, out_w);
            // wall above the joint
            shell(z_join, z_top, out_r, out_w, in_r, in_w);
            // skirt over the base step
            shell(z_join - step_h, z_join, out_r, out_w, out_r - wall / 2, out_w - wall / 2);
            // hold-downs
            for (p = lid_posts) translate([p[0], p[1], z_bt]) cylinder(d = post_d, h = z_top - z_bt);
        }
        usb_cut();
        // grooves matching the base ridges
        for (s = [-1, 1]) translate([s * (out_w - wall / 2 - fit), 0, z_join - step_h / 2])
            rotate([90, 0, 0]) cylinder(d = 1.1, h = 17, center = true, $fn = 16);
        // button / LED window over the XIAO's USB end
        translate([win[0][0], win[0][1], z_top - roof_t - 1])
            cube([win[1][0] - win[0][0], win[1][1] - win[0][1], roof_t + 2]);
        translate([4, -16, z_top - 0.4]) mirror([0, 1, 0]) linear_extrude(1)
            text("NO USB ON MAINS", size = 2.6, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
}

// ---------------------------------------------------------------- output
mirror([0, 1, 0]) {
    if (part == "base") base();
    else if (part == "lid") translate([0, 0, z_top]) rotate([180, 0, 0]) lid();   // roof down for printing
    else if (part == "print") {
        base();
        translate([2 * out_w + 6, 0, z_top]) rotate([180, 0, 0]) lid();
    } else {
        color("SteelBlue") base();
        color("LightGray", 0.6) lid();
        color("DarkGreen") translate([0, 0, z_bb]) linear_extrude(board_t) outline2d(board_r, board_half_w);
    }
}
