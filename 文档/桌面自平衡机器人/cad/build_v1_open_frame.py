import math
import os

import bpy
from mathutils import Vector

# V1 desktop self-balancing robot open-frame concept.
# All dimensions are in millimeters and converted to Blender meters.
MM = 0.001
ROOT = os.path.dirname(os.path.abspath(__file__))
BLEND_PATH = os.path.join(ROOT, "v1_open_frame.blend")
RENDER_PATH = os.path.join(ROOT, "v1_open_frame_preview.png")
STL_PATH = os.path.join(ROOT, "v1_open_frame_parts.stl")

# Ordered/measured placeholders. Update after hardware arrives.
FRAME_WIDTH = 82
FRAME_HEIGHT = 105
SPINE_THICKNESS = 4
WHEEL_DIAMETER = 43
WHEEL_WIDTH = 19
MOTOR_AXIS_Z = 23
BATTERY = (37, 18.8, 69)  # width, depth, height
BATTERY_CENTER_Z = 48
DECK = (72, 3, 48)
DECK_CENTER_Z = 80


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (
        bpy.data.meshes,
        bpy.data.curves,
        bpy.data.materials,
        bpy.data.cameras,
        bpy.data.lights,
    ):
        pass


def material(name, color, metallic=0.0, roughness=0.45):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


def cube(name, size_mm, location_mm, mat=None, bevel=1.0):
    bpy.ops.mesh.primitive_cube_add(
        location=tuple(v * MM for v in location_mm),
        scale=tuple(v * MM / 2 for v in size_mm),
    )
    obj = bpy.context.object
    obj.name = name
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        mod = obj.modifiers.new("Edge softening", "BEVEL")
        mod.width = bevel * MM
        mod.segments = 3
    if mat:
        obj.data.materials.append(mat)
    return obj


def cylinder(
    name, radius_mm, depth_mm, location_mm, rotation=(0, 0, 0), mat=None, vertices=64
):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius_mm * MM,
        depth=depth_mm * MM,
        location=tuple(v * MM for v in location_mm),
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    if mat:
        obj.data.materials.append(mat)
    return obj


def boolean_cut(target, cutter):
    mod = target.modifiers.new("Cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.solver = "EXACT"
    mod.object = cutter
    bpy.context.view_layer.objects.active = target
    while target.modifiers.find(mod.name) > 0:
        bpy.ops.object.modifier_move_up(modifier=mod.name)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def drill(target, radius_mm, depth_mm, location_mm, rotation=(math.pi / 2, 0, 0)):
    cutter = cylinder("cutter", radius_mm, depth_mm, location_mm, rotation=rotation)
    boolean_cut(target, cutter)


def rounded_slot_cut(
    target, center_mm, length_mm=12, width_mm=3.2, depth_mm=10, axis="x"
):
    offset = (length_mm - width_mm) / 2
    parts = []
    for sign in (-1, 1):
        loc = list(center_mm)
        loc[0 if axis == "x" else 2] += sign * offset
        parts.append(
            cylinder(
                "slot_end", width_mm / 2, depth_mm, loc, rotation=(math.pi / 2, 0, 0)
            )
        )
    bridge_size = [length_mm - width_mm, depth_mm, width_mm]
    if axis == "z":
        bridge_size = [width_mm, depth_mm, length_mm - width_mm]
    bridge = cube("slot_bridge", bridge_size, center_mm, bevel=0)
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts + [bridge]:
        p.select_set(True)
    bpy.context.view_layer.objects.active = bridge
    bpy.ops.object.join()
    boolean_cut(target, bridge)


def add_spine(structural):
    spine = cube(
        "PRINT_spine",
        (FRAME_WIDTH, SPINE_THICKNESS, FRAME_HEIGHT),
        (0, 0, FRAME_HEIGHT / 2),
        structural,
        2.5,
    )

    # Large windows reduce mass while retaining perimeter stiffness.
    for x in (-23, 23):
        cutter = cube("window", (29, 12, 38), (x, 0, 66), bevel=4)
        boolean_cut(spine, cutter)

    # Mounting holes for electronics deck.
    for x in (-31, 31):
        for z in (61, 99):
            drill(spine, 1.7, 12, (x, 0, z))

    # Vertical battery adjustment slots.
    for x in (-13.5, 13.5):
        rounded_slot_cut(
            spine,
            (x, 0, BATTERY_CENTER_Z),
            length_mm=24,
            width_mm=3.4,
            depth_mm=12,
            axis="z",
        )

    # Cable-tie slots.
    for x in (-29, 29):
        for z in (16, 38, 68, 94):
            rounded_slot_cut(
                spine, (x, 0, z), length_mm=10, width_mm=3.2, depth_mm=12, axis="x"
            )

    # Motor bracket reinforcement rails.
    for side in (-1, 1):
        rail = cube(
            f"PRINT_motor_rail_{side}",
            (10, 16, 43),
            (side * 36, 0, 21.5),
            structural,
            2,
        )
        for z in (14, 32):
            drill(rail, 1.2, 24, (side * 36, 0, z))
    return spine


def add_battery_tray(accent, battery_mat):
    width, depth, height = BATTERY
    tray = cube(
        "PRINT_battery_tray",
        (width + 4, depth + 4, 3),
        (0, -13, BATTERY_CENTER_Z),
        accent,
        2,
    )
    for x in (-width / 2 - 1, width / 2 + 1):
        cube(
            "PRINT_battery_lip",
            (2, depth + 4, 8),
            (x, -13, BATTERY_CENTER_Z + 2.5),
            accent,
            1,
        )
    for z in (BATTERY_CENTER_Z - height / 2 - 1, BATTERY_CENTER_Z + height / 2 + 1):
        cube("PRINT_battery_end_stop", (width + 4, 2, 8), (0, -13, z), accent, 1)
    # Visual placeholder, not exported as printable geometry.
    battery = cube(
        "PLACEHOLDER_2S_battery", BATTERY, (0, -14, BATTERY_CENTER_Z), battery_mat, 4
    )
    return tray, battery


def add_electronics_deck(deck_mat, pcb_mat, module_mat):
    deck = cube("PRINT_electronics_deck", DECK, (0, -12, DECK_CENTER_Z), deck_mat, 3)
    for x in (-31, 31):
        for z in (61, 99):
            drill(deck, 1.7, 12, (x, -12, z))
    # Universal tie slots.
    for x in (-27, -9, 9, 27):
        for z in (66, 80, 94):
            rounded_slot_cut(
                deck, (x, -12, z), length_mm=10, width_mm=3.2, depth_mm=12, axis="x"
            )

    cube("PLACEHOLDER_controller_PCB", (58, 2, 42), (-5, -15, 81), pcb_mat, 1)
    cube("PLACEHOLDER_ESP32_S3", (28, 3, 69), (23, -18, 70), module_mat, 1)
    cube("PLACEHOLDER_TB6612", (21, 4, 18), (-24, -18, 91), module_mat, 1)
    cube("PLACEHOLDER_LM2596", (21, 8, 44), (-24, -20, 64), module_mat, 1)
    return deck


def add_sensor_bracket(sensor_mat, module_mat):
    cube("PRINT_sensor_face", (34, 3, 24), (0, -8, 113), sensor_mat, 2)
    cube("PRINT_sensor_shelf", (34, 16, 3), (0, -1.5, 102.5), sensor_mat, 2)
    cube("PLACEHOLDER_ToF", (18, 3, 14), (0, -10, 114), module_mat, 1)


def add_wheels_and_motors(tire_mat, metal_mat):
    wheel_x = FRAME_WIDTH / 2 + WHEEL_WIDTH / 2 - 2
    for side in (-1, 1):
        wheel = cylinder(
            f"PLACEHOLDER_wheel_{side}",
            WHEEL_DIAMETER / 2,
            WHEEL_WIDTH,
            (side * wheel_x, 0, MOTOR_AXIS_Z),
            rotation=(0, math.pi / 2, 0),
            mat=tire_mat,
        )
        # N20 body placeholder inside each wheel side.
        cube(
            f"PLACEHOLDER_N20_{side}",
            (26, 12, 10),
            (side * 36, 0, MOTOR_AXIS_Z),
            metal_mat,
            1,
        ).rotation_euler[1] = math.pi / 2


def add_floor():
    bpy.ops.mesh.primitive_plane_add(size=0.45, location=(0, 0, 0))
    floor = bpy.context.object
    floor.name = "Presentation floor"
    floor.data.materials.append(material("Floor", (0.035, 0.04, 0.045), 0, 0.7))


def setup_scene():
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 1100
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = RENDER_PATH
    scene.render.film_transparent = False
    scene.world.color = (0.018, 0.022, 0.028)

    camera_data = bpy.data.cameras.new("Camera")
    camera = bpy.data.objects.new("Camera", camera_data)
    bpy.context.collection.objects.link(camera)
    scene.camera = camera
    camera.location = (0.18, -0.22, 0.15)
    target = Vector((0, 0, 0.058))
    camera.rotation_euler = (
        (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    )
    camera.data.lens = 58

    for name, loc, energy, size in (
        ("Key", (0.12, -0.14, 0.22), 850, 0.10),
        ("Fill", (-0.16, -0.08, 0.12), 500, 0.12),
        ("Rim", (0.04, 0.13, 0.20), 750, 0.09),
    ):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        light = bpy.data.objects.new(name, data)
        light.location = loc
        light.rotation_euler = (
            (target - light.location).to_track_quat("-Z", "Y").to_euler()
        )
        bpy.context.collection.objects.link(light)


def export_print_parts():
    bpy.ops.object.select_all(action="DESELECT")
    printable = [
        obj
        for obj in bpy.context.scene.objects
        if obj.name.startswith("PRINT_") and obj.type == "MESH"
    ]
    for obj in printable:
        obj.select_set(True)
    if printable:
        bpy.context.view_layer.objects.active = printable[0]
        try:
            bpy.ops.wm.stl_export(filepath=STL_PATH, export_selected_objects=True)
        except Exception as exc:
            print(f"STL export skipped: {exc}")


def main():
    clean_scene()
    structural = material("PETG charcoal", (0.075, 0.085, 0.095), 0.15, 0.35)
    accent = material("Battery tray", (0.07, 0.34, 0.52), 0.1, 0.38)
    deck_mat = material("Electronics deck", (0.08, 0.42, 0.31), 0.08, 0.4)
    sensor_mat = material("Sensor bracket", (0.80, 0.37, 0.08), 0.05, 0.38)
    pcb_mat = material("Controller PCB", (0.02, 0.22, 0.10), 0.0, 0.3)
    module_mat = material("Modules", (0.05, 0.18, 0.46), 0.05, 0.3)
    tire_mat = material("Rubber", (0.015, 0.018, 0.022), 0.0, 0.8)
    metal_mat = material("Motor metal", (0.38, 0.40, 0.42), 0.7, 0.25)
    battery_mat = material("Battery", (0.055, 0.06, 0.065), 0.0, 0.5)

    add_spine(structural)
    add_battery_tray(accent, battery_mat)
    add_electronics_deck(deck_mat, pcb_mat, module_mat)
    add_sensor_bracket(sensor_mat, module_mat)
    add_wheels_and_motors(tire_mat, metal_mat)
    add_floor()
    setup_scene()

    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
    export_print_parts()
    bpy.context.scene.render.filepath = RENDER_PATH
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {BLEND_PATH}")
    print(f"Rendered: {RENDER_PATH}")
    print(f"Exported: {STL_PATH}")


if __name__ == "__main__":
    main()
