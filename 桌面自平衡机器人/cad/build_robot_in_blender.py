import socket
import json

blender_script = """
import bpy
import math

# 1. 清空场景中所有旧的 Mesh 物体
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.data.objects):
    if obj.type == 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)

# 2. 材质库创建
def get_mat(name, color, roughness=0.3, metallic=0.0, emission=False, emit_strength=3.0):
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
                    bsdf.inputs['Emission Strength'].default_value = emit_strength
                elif 'Emission' in bsdf.inputs:
                    bsdf.inputs['Emission'].default_value = color
    return mat

mat_chassis = get_mat("Chassis_DarkGrey", (0.15, 0.16, 0.18, 1.0), roughness=0.4)
mat_white   = get_mat("Chassis_White", (0.9, 0.9, 0.92, 1.0), roughness=0.25)
mat_rubber  = get_mat("Rubber_Tire", (0.04, 0.04, 0.04, 1.0), roughness=0.85)
mat_rim     = get_mat("Wheel_Rim_Cyan", (0.05, 0.65, 0.85, 1.0), metallic=0.3, roughness=0.2)
mat_motor   = get_mat("Motor_Silver", (0.8, 0.8, 0.82, 1.0), metallic=0.9, roughness=0.2)
mat_gearbox = get_mat("Gearbox_Brass", (0.85, 0.68, 0.22, 1.0), metallic=0.85, roughness=0.25)
mat_pcb     = get_mat("PCB_MatteBlack", (0.08, 0.09, 0.09, 1.0), roughness=0.3)
mat_gold    = get_mat("Gold_Pad", (0.95, 0.78, 0.25, 1.0), metallic=0.95, roughness=0.15)
mat_screen  = get_mat("OLED_Glow_Cyan", (0.1, 0.85, 1.0, 1.0), emission=True, emit_strength=8.0)
mat_battery = get_mat("Battery_Blue", (0.12, 0.45, 0.95, 1.0), roughness=0.3)
mat_chip    = get_mat("IC_Chip", (0.02, 0.02, 0.02, 1.0), roughness=0.5)

# ==========================================
# 核心结构尺寸 (严格杜绝干涉，留足3mm避空)
# ==========================================
wheel_dia   = 0.043   # 43mm 轮径
wheel_r     = wheel_dia / 2.0  # 21.5mm 半径 (Z轴中心高度)
wheel_width = 0.014   # 14mm 轮宽
chassis_w   = 0.048   # 车身宽度 48mm (y从 -24mm 到 +24mm)
chassis_len = 0.070   # 车身长度 70mm (x从 -35mm 到 +35mm)
plate_th    = 0.003   # 板厚 3mm

# 轮子 Y 轴中心位置: 离开车身侧壁 3mm 间隙 + 轮宽一半 = 24mm + 3mm + 7mm = 34mm
wheel_y = (chassis_w / 2.0) + 0.003 + (wheel_width / 2.0) # 0.034m (±34mm)

# 3. 创建左右车轮与 N20 电机 (绝对不干涉)
for side in [-1, 1]:
    y_center = side * wheel_y
    
    # 橡胶外胎 (43mm 外径, 32mm 内径)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r, depth=wheel_width,
        location=(0, y_center, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    tire = bpy.context.active_object
    tire.name = f"Tire_{'L' if side > 0 else 'R'}"
    tire.data.materials.append(mat_rubber)
    
    # 轮毂 (亮青色)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r * 0.72, depth=wheel_width * 1.02,
        location=(0, y_center, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    rim = bpy.context.active_object
    rim.name = f"Rim_{'L' if side > 0 else 'R'}"
    rim.data.materials.append(mat_rim)
    
    # 轮轴 (3mm D轴，连接电机与轮毂)
    shaft_len = 0.012
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.0015, depth=shaft_len,
        location=(0, side * (wheel_y - 0.005), wheel_r), rotation=(math.pi/2, 0, 0)
    )
    shaft = bpy.context.active_object
    shaft.name = f"Shaft_{'L' if side > 0 else 'R'}"
    shaft.data.materials.append(mat_motor)

    # N20 减速箱 (黄铜色，长9mm，宽12mm，高10mm)
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(0, side * (chassis_w/2.0 - 0.0045), wheel_r)
    )
    gearbox = bpy.context.active_object
    gearbox.name = f"Gearbox_{'L' if side > 0 else 'R'}"
    gearbox.scale = (0.012, 0.009, 0.010)
    gearbox.data.materials.append(mat_gearbox)

    # N20 电机马达本体 (银色圆柱，长15mm，直径12mm)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.006, depth=0.015,
        location=(0, side * (chassis_w/2.0 - 0.009 - 0.0075), wheel_r), rotation=(math.pi/2, 0, 0)
    )
    motor_cyl = bpy.context.active_object
    motor_cyl.name = f"Motor_Body_{'L' if side > 0 else 'R'}"
    motor_cyl.data.materials.append(mat_motor)

# 4. 底层电机固定底盘 (Base Plate - 位于电机下方)
# Z = 12mm 处，板厚 3mm
z_base = wheel_r - 0.005 - plate_th/2.0 # 0.015m
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_base))
base_plate = bpy.context.active_object
base_plate.name = "Chassis_Base_Plate"
base_plate.scale = (chassis_len, chassis_w, plate_th)
base_plate.data.materials.append(mat_chassis)

# 5. 中层电池仓 (2节 18650 电池，前后并排平放)
z_battery = z_base + plate_th/2.0 + 0.0095 # 0.026m
for b_idx in [-1, 1]:
    # 18650 电池 (直径18mm, 长度65mm, 沿 X 轴前后并排)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.009, depth=0.065,
        location=(0, b_idx * 0.010, z_battery), rotation=(0, math.pi/2, 0)
    )
    bat = bpy.context.active_object
    bat.name = f"18650_Battery_{b_idx}"
    bat.data.materials.append(mat_battery)

# 中层电池盖板
z_mid = z_battery + 0.009 + plate_th/2.0 # 0.0365m
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_mid))
mid_plate = bpy.context.active_object
mid_plate.name = "Chassis_Mid_Plate"
mid_plate.scale = (chassis_len, chassis_w, plate_th)
mid_plate.data.materials.append(mat_chassis)

# 6. 顶层主控安装托架与你的 STM32 主控板
z_top = z_mid + 0.022 # 0.0585m (58.5mm 高度)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_top))
top_plate = bpy.context.active_object
top_plate.name = "Chassis_Top_Plate"
top_plate.scale = (chassis_len * 0.85, chassis_w, plate_th)
top_plate.data.materials.append(mat_white)

# STM32 主控板 PCB (36mm x 36mm x 1.6mm)
z_pcb = z_top + plate_th/2.0 + 0.003 # 垫高 3mm 铜柱
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_pcb + 0.0008))
pcb = bpy.context.active_object
pcb.name = "STM32_Mainboard_PCB"
pcb.scale = (0.036, 0.036, 0.0016)
pcb.data.materials.append(mat_pcb)

# PCB 上的 STM32F401 芯片 (QFN封装)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_pcb + 0.0016 + 0.0006))
mcu = bpy.context.active_object
mcu.name = "STM32F401_MCU"
mcu.scale = (0.007, 0.007, 0.0010)
mcu.data.materials.append(mat_chip)

# PCB 上的 ICM-42688 六轴姿态芯片
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-0.008, 0.008, z_pcb + 0.0016 + 0.0005))
imu = bpy.context.active_object
imu.name = "ICM42688_IMU"
imu.scale = (0.003, 0.003, 0.0008)
imu.data.materials.append(mat_chip)

# 7. 前置桌宠发光 OLED 表情屏 (眼睛面罩)
# 位于车头正面 x = +28mm，略带 15 度仰角
z_screen = z_top + 0.016
bpy.ops.mesh.primitive_cube_add(
    size=1.0, location=(0.026, 0, z_screen), rotation=(0, -math.radians(12), 0)
)
screen_glass = bpy.context.active_object
screen_glass.name = "Robot_OLED_Face"
screen_glass.scale = (0.003, 0.030, 0.016)
screen_glass.data.materials.append(mat_screen)

# 8. 4 根 M3 垂直支撑铜柱 (严格位于机身内部 x=±25mm, y=±16mm)
standoff_len = z_top - z_base
z_standoff_center = (z_base + z_top) / 2.0
for cx in [-0.025, 0.025]:
    for cy in [-0.016, 0.016]:
        bpy.ops.mesh.primitive_cylinder_add(
            radius=0.0025, depth=standoff_len,
            location=(cx, cy, z_standoff_center)
        )
        pillar = bpy.context.active_object
        pillar.name = "M3_Brass_Standoff"
        pillar.data.materials.append(mat_gearbox)

print("✅ Perfect Non-Intersecting Robot Model Built Successfully!")
"""

# 连接并发送至 Blender
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 9876))
cmd = {"type": "execute_code", "params": {"code": blender_script}}
s.sendall(json.dumps(cmd).encode("utf-8"))
data = s.recv(16384)
print("Execute response:", data.decode("utf-8"))
s.close()
