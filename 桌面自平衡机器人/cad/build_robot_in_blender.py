import socket
import json

blender_script = """
import bpy
import math

# 1. 清空现有场景中的默认物体 (可选)
for obj in list(bpy.data.objects):
    if obj.type == 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)

# 2. 辅助材质创建函数
def create_mat(name, color, roughness=0.3, metallic=0.0, emission=False):
    mat = bpy.data.materials.get(name)
    if not mat:
        mat = bpy.data.materials.new(name=name)
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        bsdf = nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs['Base Color'].default_value = color
            bsdf.inputs['Roughness'].default_value = roughness
            bsdf.inputs['Metallic'].default_value = metallic
            if emission:
                if 'Emission Color' in bsdf.inputs:
                    bsdf.inputs['Emission Color'].default_value = color
                    bsdf.inputs['Emission Strength'].default_value = 5.0
                elif 'Emission' in bsdf.inputs:
                    bsdf.inputs['Emission'].default_value = color
    return mat

mat_chassis = create_mat("Chassis_Plastic", (0.12, 0.12, 0.14, 1.0), roughness=0.4)
mat_accent  = create_mat("Accent_Yellow", (1.0, 0.65, 0.05, 1.0), roughness=0.3)
mat_rubber   = create_mat("Rubber_Tire", (0.05, 0.05, 0.05, 1.0), roughness=0.8)
mat_rim      = create_mat("Wheel_Rim", (0.9, 0.9, 0.95, 1.0), metallic=0.7, roughness=0.2)
mat_brass    = create_mat("Brass_Standoff", (0.85, 0.65, 0.2, 1.0), metallic=0.9, roughness=0.2)
mat_pcb      = create_mat("PCB_Green", (0.02, 0.25, 0.08, 1.0), roughness=0.3)
mat_screen   = create_mat("OLED_Screen", (0.0, 0.6, 1.0, 1.0), emission=True)
mat_battery  = create_mat("Battery_18650", (0.1, 0.45, 0.9, 1.0), roughness=0.3)

# 3. 创建车轮与电机 (左右对称)
wheel_radius = 0.0215
wheel_width  = 0.012
wheel_track  = 0.076  # 轮距

for side in [-1, 1]:
    y_pos = side * (wheel_track / 2.0)
    
    # 橡胶轮胎 (外圆环)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_radius, depth=wheel_width, 
        location=(0, y_pos, wheel_radius), rotation=(math.pi/2, 0, 0)
    )
    tire = bpy.context.active_object
    tire.name = f"Tire_{'L' if side > 0 else 'R'}"
    tire.data.materials.append(mat_rubber)
    
    # 轮毂
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_radius*0.75, depth=wheel_width*1.05, 
        location=(0, y_pos, wheel_radius), rotation=(math.pi/2, 0, 0)
    )
    rim = bpy.context.active_object
    rim.name = f"Rim_{'L' if side > 0 else 'R'}"
    rim.data.materials.append(mat_rim)

    # N20 减速电机本体 (金银色方块)
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(0, side * 0.022, wheel_radius)
    )
    motor = bpy.context.active_object
    motor.name = f"N20_Motor_{'L' if side > 0 else 'R'}"
    motor.scale = (0.012, 0.024, 0.010)
    motor.data.materials.append(mat_brass)

# 4. 底层电机安装底盘 (Base Plate)
z_base = wheel_radius + 0.008
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_base))
base_plate = bpy.context.active_object
base_plate.name = "Chassis_Base_Plate"
base_plate.scale = (0.046, 0.075, 0.003)
base_plate.data.materials.append(mat_chassis)

# 5. 中层电池仓 (2节 18650 电池)
z_mid = z_base + 0.022
for b_idx in [-1, 1]:
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.009, depth=0.065,
        location=(b_idx * 0.011, 0, z_mid), rotation=(math.pi/2, 0, 0)
    )
    bat = bpy.context.active_object
    bat.name = f"Battery_18650_{b_idx}"
    bat.data.materials.append(mat_battery)

# 中层电池固定隔板
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_mid + 0.012))
mid_plate = bpy.context.active_object
mid_plate.name = "Chassis_Mid_Plate"
mid_plate.scale = (0.046, 0.075, 0.003)
mid_plate.data.materials.append(mat_chassis)

# 6. 顶层大脑主控层 (STM32 + ICM42688 主控板)
z_top = z_mid + 0.025
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_top))
pcb = bpy.context.active_object
pcb.name = "STM32_Main_Controller_PCB"
pcb.scale = (0.036, 0.036, 0.0016)
pcb.data.materials.append(mat_pcb)

# PCB 上面的主控芯片 (STM32 QFN / ICM42688)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_top + 0.0015))
chip = bpy.context.active_object
chip.name = "STM32F401_MCU"
chip.scale = (0.007, 0.007, 0.0012)
chip.data.materials.append(mat_rubber)

# 7. 头部桌宠表情屏 (0.96寸 OLED / 眼睛面罩)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.023, 0, z_top + 0.012))
screen = bpy.context.active_object
screen.name = "Robot_OLED_Face"
screen.scale = (0.002, 0.028, 0.015)
screen.data.materials.append(mat_screen)

# 8. 4 根垂直支撑铜柱 (M3 Brass Standoffs)
for cx in [-0.018, 0.018]:
    for cy in [-0.032, 0.032]:
        bpy.ops.mesh.primitive_cylinder_add(
            radius=0.0025, depth=z_top - z_base,
            location=(cx, cy, (z_base + z_top)/2.0)
        )
        pillar = bpy.context.active_object
        pillar.name = "Brass_Standoff"
        pillar.data.materials.append(mat_brass)

print("✅ Desktop Self-Balancing Robot 3D Model created successfully in Blender!")
"""

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 9876))
cmd = {"type": "execute_code", "params": {"code": blender_script}}
s.sendall(json.dumps(cmd).encode("utf-8"))
data = s.recv(16384)
print("Blender Execution Response:", data.decode("utf-8"))
s.close()
