// V1 desktop self-balancing robot open frame
// Units: millimeters. Export one part at a time with `part`.

$fn = 48;

part = "assembly"; // assembly, spine, battery_tray, electronics_deck, sensor_bracket

// ---------- Measured / ordered hardware placeholders ----------
wheel_diameter = 43;
wheel_width = 19;

n20_body_width = 12;
n20_body_height = 10;
n20_body_length = 26;
shaft_axis_height = wheel_diameter / 2;

// Ordered 2S pack shown as approximately 69 x 37 x 18.8 mm.
battery_length = 69;
battery_width = 37;
battery_height = 18.8;
battery_clearance = 1.2;

// Update these after measuring the actual boards.
controller_length = 58;
controller_width = 42;
esp32_length = 69;
esp32_width = 28;
tb6612_length = 21;
tb6612_width = 18;
lm2596_length = 44;
lm2596_width = 21;

// ---------- Frame geometry ----------
frame_width = 82;
frame_height = 105;
spine_thickness = 4;
side_rail_width = 10;
rail_depth = 16;

motor_axis_z = 23;
motor_center_x = frame_width / 2 + wheel_width / 2 - 2;

battery_center_z = 48;
battery_adjust_range = 24;

deck_width = 72;
deck_height = 48;
deck_thickness = 3;
deck_center_z = 80;

m3_hole = 3.4;
m2_hole = 2.4;
strap_slot_width = 3.2;
strap_slot_length = 13;

explode = part == "assembly" ? 0 : 0;

module rounded_box(size = [10, 10, 10], radius = 2) {
    hull() {
        for (x = [radius, size[0] - radius])
            for (y = [radius, size[1] - radius])
                translate([x, y, 0]) cylinder(r = radius, h = size[2]);
    }
}

module slot(length = 12, width = 3, height = 10) {
    hull() {
        translate([-length / 2 + width / 2, 0, 0]) cylinder(d = width, h = height);
        translate([ length / 2 - width / 2, 0, 0]) cylinder(d = width, h = height);
    }
}

module vertical_spine() {
    difference() {
        union() {
            // Main vertical plate. X is robot width, Z is height.
            translate([-frame_width / 2, -spine_thickness / 2, 0])
                cube([frame_width, spine_thickness, frame_height]);

            // Side rails stiffen the motor area.
            for (side = [-1, 1])
                translate([side * (frame_width / 2 - side_rail_width / 2),
                           -rail_depth / 2,
                           0])
                    cube([side_rail_width, rail_depth, 43]);
        }

        // Reduce weight while retaining a perimeter and central electronics rail.
        for (side = [-1, 1])
            translate([side * 23 - 15, -spine_thickness, 45])
                rounded_box([30, spine_thickness * 3, 38], 4);

        // M3 deck mounting holes.
        for (x = [-deck_width / 2 + 5, deck_width / 2 - 5])
            for (z = [deck_center_z - deck_height / 2 + 5,
                      deck_center_z + deck_height / 2 - 5])
                translate([x, -spine_thickness, z])
                    rotate([-90, 0, 0]) cylinder(d = m3_hole, h = spine_thickness * 3);

        // Battery tray adjustment slots.
        for (x = [-battery_width / 2 + 5, battery_width / 2 - 5])
            translate([x, -spine_thickness, battery_center_z])
                rotate([-90, 0, 0])
                    hull() {
                        translate([0, -battery_adjust_range / 2, 0]) cylinder(d = m3_hole, h = spine_thickness * 3);
                        translate([0,  battery_adjust_range / 2, 0]) cylinder(d = m3_hole, h = spine_thickness * 3);
                    }

        // Cable tie slots.
        for (x = [-28, 28])
            for (z = [16, 38, 67, 94])
                translate([x, -spine_thickness, z])
                    rotate([-90, 0, 0]) slot(10, strap_slot_width, spine_thickness * 3);
    }
}

module motor_mount_holes() {
    // Generic two-hole pattern for the ordered metal N20 brackets.
    // Measure and update `motor_hole_pitch` when the brackets arrive.
    motor_hole_pitch = 18;
    for (side = [-1, 1])
        for (zoff = [-motor_hole_pitch / 2, motor_hole_pitch / 2])
            translate([side * (frame_width / 2 - side_rail_width / 2),
                       -rail_depth,
                       motor_axis_z + zoff])
                rotate([-90, 0, 0]) cylinder(d = m2_hole, h = rail_depth * 2);
}

module spine_part() {
    difference() {
        vertical_spine();
        motor_mount_holes();
    }
}

module battery_tray() {
    tray_length = battery_length + 2 * battery_clearance;
    tray_width = battery_width + 2 * battery_clearance;
    tray_floor = 3;
    lip = 4;

    difference() {
        union() {
            translate([-tray_width / 2, -tray_length / 2, 0])
                rounded_box([tray_width, tray_length, tray_floor], 3);
            for (x = [-tray_width / 2, tray_width / 2 - 2])
                translate([x, -tray_length / 2, 0]) cube([2, tray_length, lip + tray_floor]);
            for (y = [-tray_length / 2, tray_length / 2 - 2])
                translate([-tray_width / 2, y, 0]) cube([tray_width, 2, lip + tray_floor]);
        }

        // 10 mm hook-and-loop strap passages.
        for (y = [-tray_length / 4, tray_length / 4])
            for (x = [-tray_width / 2 + 5, tray_width / 2 - 5])
                translate([x, y, -1]) slot(strap_slot_length, strap_slot_width, tray_floor + 2);

        // M3 fixing holes to the spine's sliding slots.
        for (x = [-battery_width / 2 + 5, battery_width / 2 - 5])
            translate([x, 0, -1]) cylinder(d = m3_hole, h = tray_floor + 2);
    }
}

module electronics_deck() {
    difference() {
        translate([-deck_width / 2, -deck_height / 2, 0])
            rounded_box([deck_width, deck_height, deck_thickness], 4);

        // M3 holes to attach the deck to the vertical spine.
        for (x = [-deck_width / 2 + 5, deck_width / 2 - 5])
            for (y = [-deck_height / 2 + 5, deck_height / 2 - 5])
                translate([x, y, -1]) cylinder(d = m3_hole, h = deck_thickness + 2);

        // Universal module slots instead of guessed board holes.
        for (x = [-27, -9, 9, 27])
            for (y = [-16, 0, 16])
                translate([x, y, -1]) slot(10, strap_slot_width, deck_thickness + 2);
    }
}

module sensor_bracket() {
    plate_width = 34;
    plate_height = 24;
    plate_thickness = 3;

    difference() {
        union() {
            translate([-plate_width / 2, 0, 0])
                rounded_box([plate_width, plate_thickness, plate_height], 2);
            translate([-plate_width / 2, 0, 0])
                cube([plate_width, 16, plate_thickness]);
        }

        // Universal slots for ToF now and a future camera/sensor module.
        for (x = [-10, 10])
            translate([x, -1, plate_height / 2])
                rotate([-90, 0, 0]) slot(8, m2_hole, plate_thickness + 2);

        for (x = [-11, 11])
            translate([x, 10, -1]) cylinder(d = m3_hole, h = plate_thickness + 2);
    }
}

module wheel_placeholder(side = 1) {
    color([0.08, 0.08, 0.08, 0.45])
        translate([side * motor_center_x, 0, motor_axis_z])
            rotate([90, 0, 0]) cylinder(d = wheel_diameter, h = wheel_width, center = true);
}

module battery_placeholder() {
    color([0.1, 0.1, 0.1, 0.55])
        translate([-battery_width / 2, -battery_height / 2, battery_center_z - battery_length / 2])
            cube([battery_width, battery_height, battery_length]);
}

module board_placeholder(size, pos, tint) {
    color(tint)
        translate([pos[0] - size[0] / 2, pos[1], pos[2] - size[1] / 2])
            cube([size[0], 2, size[1]]);
}

module assembly() {
    color([0.72, 0.74, 0.76]) spine_part();

    // Battery tray is shown vertical against the spine for packaging review.
    color([0.20, 0.42, 0.64])
        translate([0, -battery_height / 2 - spine_thickness / 2 - 2, battery_center_z])
            rotate([90, 0, 0]) battery_tray();

    color([0.18, 0.55, 0.43])
        translate([0, -spine_thickness / 2 - 11, deck_center_z])
            rotate([90, 0, 0]) electronics_deck();

    color([0.85, 0.55, 0.15])
        translate([0, -spine_thickness / 2 - 9, frame_height - 22]) sensor_bracket();

    wheel_placeholder(-1);
    wheel_placeholder(1);
    battery_placeholder();

    board_placeholder([controller_length, controller_width], [0, -15, 82], [0.1, 0.55, 0.25, 0.55]);
    board_placeholder([esp32_width, esp32_length], [24, -18, 70], [0.1, 0.25, 0.7, 0.55]);
}

if (part == "assembly") assembly();
else if (part == "spine") spine_part();
else if (part == "battery_tray") battery_tray();
else if (part == "electronics_deck") electronics_deck();
else if (part == "sensor_bracket") sensor_bracket();
