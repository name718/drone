import socket
import json

blender_script = """
import bpy
import math

# 1. 清空旧物体
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.data.objects):
    if obj.type == 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)

# 2. 高级材质库
def get_mat(name, color, roughness=0.3, metallic=0.0, emission=False, alpha=1.0, emit_strength=5.0):
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
            if 'Alpha' in bsdf.inputs:
                bsdf.inputs['Alpha'].default_value = alpha
            if emission:
                if 'Emission Color' in bsdf.inputs:
                    bsdf.inputs['Emission Color'].default_value = color
                    bsdf.inputs['Emission Strength'].default_value = emit_strength
                elif 'Emission' in bsdf.inputs:
                    bsdf.inputs['Emission'].default_value = color
        if alpha < 1.0:
            mat.blend_method = 'BLEND'
    return mat

mat_shell_white = get_mat("Armor_White", (0.92, 0.93, 0.95, 1.0), roughness=0.2, metallic=0.05)
mat_shell_dark  = get_mat("Armor_DarkTitanium", (0.12, 0.13, 0.15, 1.0), roughness=0.35, metallic=0.2)
mat_visor_glass = get_mat("Visor_Glass", (0.02, 0.03, 0.05, 1.0), roughness=0.05, metallic=0.1)
mat_glow_cyan   = get_mat("Cyber_Cyan_Glow", (0.0, 0.85, 1.0, 1.0), emission=True, emit_strength=10.0)
mat_glow_orange = get_mat("Cyber_Orange_Glow", (1.0, 0.45, 0.05, 1.0), emission=True, emit_strength=6.0)
mat_tire_rubber = get_mat("Tire_SoftRubber", (0.03, 0.03, 0.03, 1.0), roughness=0.9)
mat_rim_alu     = get_mat("Rim_GunmetalAlu", (0.25, 0.26, 0.28, 1.0), roughness=0.25, metallic=0.9)
mat_brass_nut   = get_mat("Brass_Nut", (0.9, 0.72, 0.2, 1.0), roughness=0.2, metallic=0.95)

# ==============================================================
# 比例与姿态精细化调校 (更呆萌紧凑、轮身比例完美协调)
# ==============================================================
wheel_r    = 0.024   # 48mm 越野大轮径 (半径 24mm，更霸气沉稳)
wheel_w    = 0.016   # 16mm 宽胎
body_w     = 0.052   # 机身外宽 52mm
body_l     = 0.068   # 机身长 68mm
wheel_y    = (body_w / 2.0) + 0.005 + (wheel_w / 2.0) # 39mm 轮距

# ==============================================================
# 3. 真实 5 幅轻量化运动轮毂 + 倒角越野防滑轮胎
# ==============================================================
for side in [-1, 1]:
    y_pos = side * wheel_y
    
    # ① 橡胶主轮胎 (带倒角平滑轮肩)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r, depth=wheel_w,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    tire = bpy.context.active_object
    tire.name = f"Real_Tire_{'L' if side>0 else 'R'}"
    tire.data.materials.append(mat_tire_rubber)
    sub_t = tire.modifiers.new(name="Bevel", type='BEVEL')
    sub_t.width = 0.25
    sub_t.segments = 3

    # ② 轮毂外圈金属轮缘 (Gunmetal Rim)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r * 0.78, depth=wheel_w * 1.02,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    rim_outer = bpy.context.active_object
    rim_outer.name = f"Rim_Outer_{'L' if side>0 else 'R'}"
    rim_outer.data.materials.append(mat_rim_alu)

    # ③ 5 根运动轮辐 (5-Spoke Star Design)
    for i in range(5):
        angle = i * (2 * math.pi / 5.0)
        spoke_x = math.cos(angle) * (wheel_r * 0.42)
        spoke_z = wheel_r + math.sin(angle) * (wheel_r * 0.42)
        
        bpy.ops.mesh.primitive_cube_add(
            size=1.0, location=(spoke_x, y_pos + side * 0.001, spoke_z),
            rotation=(0, -angle, 0)
        )
        spoke = bpy.context.active_object
        spoke.name = f"Spoke_{i}_{'L' if side>0 else 'R'}"
        spoke.scale = (0.003, wheel_w * 0.98, wheel_r * 0.38)
        spoke.data.materials.append(mat_rim_alu)

    # ④ 轮毂中央金属锁紧法兰与发光轴心 (Hub Center Cap)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.006, depth=wheel_w * 1.15,
        location=(0, y_pos + side * 0.002, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    hub_nut = bpy.context.active_object
    hub_nut.name = f"Hub_Nut_{'L' if side>0 else 'R'}"
    hub_nut.data.materials.append(mat_brass_nut)

    # ⑤ 金属驱动传动轴 (连接电机与轮毂内侧)
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.003, depth=0.012,
        location=(0, side * (body_w/2.0 + 0.002), wheel_r), rotation=(math.pi/2, 0, 0)
    )
    axle = bpy.context.active_object
    axle.name = f"Drive_Axle_{'L' if side>0 else 'R'}"
    axle.data.materials.append(mat_rim_alu)

# ==============================================================
# 4. 机身躯干 (比例更协调、更有萌态与未来感)
# ==============================================================
# 底盘离地间隙 12mm，整车重心黄金分布
z_torso = wheel_r + 0.020
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_torso))
torso = bpy.context.active_object
torso.name = "Robot_Torso"
torso.scale = (body_l, body_w, 0.038)
torso.data.materials.append(mat_shell_white)
sub_body = torso.modifiers.new(name="Bevel", type='BEVEL')
sub_body.width = 0.14
sub_body.segments = 4

# 胸前科技感核心与呼吸灯条
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(body_l/2.0 + 0.001, 0, z_torso))
chest_stripe = bpy.context.active_object
chest_stripe.name = "Chest_Stripe"
chest_stripe.scale = (0.002, 0.034, 0.008)
chest_stripe.data.materials.append(mat_shell_dark)

bpy.ops.mesh.primitive_cube_add(size=1.0, location=(body_l/2.0 + 0.002, 0, z_torso))
chest_glow = bpy.context.active_object
chest_glow.name = "Chest_Energy_Core"
chest_glow.scale = (0.0015, 0.018, 0.004)
chest_glow.data.materials.append(mat_glow_orange)

# ==============================================================
# 5. 可爱宇航头盔头部与发光面罩
# ==============================================================
z_head = z_torso + 0.032
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.002, 0, z_head))
head = bpy.context.active_object
head.name = "Robot_Head"
head.scale = (0.048, 0.052, 0.032)
head.data.materials.append(mat_shell_white)
sub_h = head.modifiers.new(name="Bevel", type='BEVEL')
sub_h.width = 0.18
sub_h.segments = 5

# 深黑弧形高光面罩 (Visor)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.026, 0, z_head))
visor = bpy.context.active_object
visor.name = "Face_Visor"
visor.scale = (0.003, 0.044, 0.022)
visor.data.materials.append(mat_visor_glass)
sub_v = visor.modifiers.new(name="Bevel", type='BEVEL')
sub_v.width = 0.15
sub_v.segments = 4

# 面罩内的大眼萌像素眼睛 (OLED Pixel Eyes)
for eye_side in [-1, 1]:
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(0.028, eye_side * 0.012, z_head + 0.001)
    )
    eye = bpy.context.active_object
    eye.name = f"Eye_{'L' if eye_side>0 else 'R'}"
    eye.scale = (0.002, 0.007, 0.009)
    eye.data.materials.append(mat_glow_cyan)

# 侧边科技小耳朵 (Cyber Ears)
for ear_side in [-1, 1]:
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.007, depth=0.006,
        location=(0.002, ear_side * (0.028 + 0.003), z_head), rotation=(math.pi/2, 0, 0)
    )
    ear = bpy.context.active_object
    ear.name = f"Ear_{'L' if ear_side>0 else 'R'}"
    ear.data.materials.append(mat_shell_dark)
    
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.004, depth=0.007,
        location=(0.002, ear_side * (0.028 + 0.0035), z_head), rotation=(math.pi/2, 0, 0)
    )
    ear_glow = bpy.context.active_object
    ear_glow.name = f"Ear_Glow_{'L' if ear_side>0 else 'R'}"
    ear_glow.data.materials.append(mat_glow_cyan)

# 6. 后背背包仓 (背包装入 Type-C 充电板与电源开关)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-body_l/2.0 - 0.004, 0, z_torso + 0.003))
backpack = bpy.context.active_object
backpack.name = "Backpack_Power_Unit"
backpack.scale = (0.012, 0.038, 0.030)
backpack.data.materials.append(mat_shell_dark)
sub_b = backpack.modifiers.new(name="Bevel", type='BEVEL')
sub_b.width = 0.15
sub_b.segments = 3

# 平滑所有多边形
for obj in bpy.data.objects:
    if obj.type == "MESH":
        for poly in obj.data.polygons:
            poly.use_smooth = True

print("✅ High-Realism Wheels and Balanced Robot Model Built Successfully!")
"""

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 9876))
cmd = {"type": "execute_code", "params": {"code": blender_script}}
s.sendall(json.dumps(cmd).encode("utf-8"))
data = s.recv(16384)
print("Execute response:", data.decode("utf-8"))
s.close()
