import socket
import json

blender_script = """
import bpy
import math

# 1. 清空场景中旧物体
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.data.objects):
    if obj.type == 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)

# 2. 高级材质库 (太空宇航白 + 赛博黑钛 + 发光青蓝 + 哑光橡胶)
def get_mat(name, color, roughness=0.3, metallic=0.0, emission=False, emit_strength=5.0):
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

mat_shell_white = get_mat("Armor_White", (0.92, 0.93, 0.95, 1.0), roughness=0.2, metallic=0.05)
mat_shell_dark  = get_mat("Armor_DarkTitanium", (0.12, 0.13, 0.15, 1.0), roughness=0.35, metallic=0.2)
mat_visor_glass = get_mat("Visor_Glass", (0.02, 0.03, 0.05, 1.0), roughness=0.05, metallic=0.1)
mat_glow_cyan   = get_mat("Cyber_Cyan_Glow", (0.0, 0.85, 1.0, 1.0), emission=True, emit_strength=10.0)
mat_glow_orange = get_mat("Cyber_Orange_Glow", (1.0, 0.45, 0.05, 1.0), emission=True, emit_strength=6.0)
mat_rubber      = get_mat("Tire_Rubber", (0.04, 0.04, 0.04, 1.0), roughness=0.85)
mat_rim_metal   = get_mat("Wheel_Rim_Metal", (0.85, 0.85, 0.88, 1.0), roughness=0.2, metallic=0.8)

# 基础机械参数
wheel_r = 0.0215  # 43mm 轮径
wheel_w = 0.014   # 14mm 轮宽
body_w  = 0.052   # 车身宽 52mm
body_l  = 0.058   # 车身长 58mm
body_h  = 0.065   # 车身高 65mm

# 3. 头部宇航盔 (圆润胶囊科技感头部)
z_head = wheel_r + 0.052
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.002, 0, z_head))
head = bpy.context.active_object
head.name = "Robot_Helmet_Head"
head.scale = (0.046, 0.052, 0.036)
head.data.materials.append(mat_shell_white)

# 头部倒角细分 (让头盔圆润拟人化)
sub = head.modifiers.new(name="Bevel", type='BEVEL')
sub.width = 0.18
sub.segments = 5

# 4. 前置一体化弧形深黑面罩 (Visor)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.024, 0, z_head))
visor = bpy.context.active_object
visor.name = "Face_Visor"
visor.scale = (0.004, 0.044, 0.024)
visor.data.materials.append(mat_visor_glass)
sub_v = visor.modifiers.new(name="Bevel", type='BEVEL')
sub_v.width = 0.15
sub_v.segments = 4

# 面罩上的发光科技眼睛 (OLED Pixel Eyes)
for eye_side in [-1, 1]:
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(0.0265, eye_side * 0.012, z_head + 0.002)
    )
    eye = bpy.context.active_object
    eye.name = f"Eye_{'L' if eye_side>0 else 'R'}"
    eye.scale = (0.002, 0.006, 0.008)
    eye.data.materials.append(mat_glow_cyan)

# 5. 侧边机甲耳机 / 科技小耳朵 (Cyberpunk Ears)
for ear_side in [-1, 1]:
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.008, depth=0.006,
        location=(0.002, ear_side * (0.028 + 0.003), z_head), rotation=(math.pi/2, 0, 0)
    )
    ear = bpy.context.active_object
    ear.name = f"Ear_Antenna_{'L' if ear_side>0 else 'R'}"
    ear.data.materials.append(mat_shell_dark)
    
    # 耳机发光光环
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.005, depth=0.007,
        location=(0.002, ear_side * (0.028 + 0.0035), z_head), rotation=(math.pi/2, 0, 0)
    )
    ear_glow = bpy.context.active_object
    ear_glow.name = f"Ear_Glow_{'L' if ear_side>0 else 'R'}"
    ear_glow.data.materials.append(mat_glow_cyan)

# 6. 中段机甲躯干外壳 (Torso Armor - 把电池和主控完美包裹)
z_torso = wheel_r + 0.024
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_torso))
torso = bpy.context.active_object
torso.name = "Torso_Armor"
torso.scale = (body_l, body_w, 0.038)
torso.data.materials.append(mat_shell_white)
sub_t = torso.modifiers.new(name="Bevel", type='BEVEL')
sub_t.width = 0.12
sub_t.segments = 4

# 胸口科技装饰条与状态指示灯
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(body_l/2.0 + 0.001, 0, z_torso))
chest_stripe = bpy.context.active_object
chest_stripe.name = "Chest_LED_Stripe"
chest_stripe.scale = (0.002, 0.032, 0.006)
chest_stripe.data.materials.append(mat_shell_dark)

bpy.ops.mesh.primitive_cube_add(size=1.0, location=(body_l/2.0 + 0.002, 0, z_torso))
chest_led = bpy.context.active_object
chest_led.name = "Chest_Core_LED"
chest_led.scale = (0.0015, 0.016, 0.003)
chest_led.data.materials.append(mat_glow_orange)

# 7. 后背背包仓 (Backpack Power Unit - 放置 Type-C 与电源开关)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-body_l/2.0 - 0.004, 0, z_torso + 0.004))
backpack = bpy.context.active_object
backpack.name = "Backpack_Battery_Module"
backpack.scale = (0.012, 0.036, 0.032)
backpack.data.materials.append(mat_shell_dark)
sub_b = backpack.modifiers.new(name="Bevel", type='BEVEL')
sub_b.width = 0.15
sub_b.segments = 3

# 8. 左右车轮总成 (赛博轮毂 + 宽胎 + 发光轮圈)
wheel_y = (body_w / 2.0) + 0.004 + (wheel_w / 2.0) # 37mm

for side in [-1, 1]:
    y_pos = side * wheel_y
    
    # 橡胶花纹轮胎
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r, depth=wheel_w,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    tire = bpy.context.active_object
    tire.name = f"Tire_{'L' if side>0 else 'R'}"
    tire.data.materials.append(mat_rubber)
    
    # 金属三幅轮毂
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r * 0.76, depth=wheel_w * 1.05,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    rim = bpy.context.active_object
    rim.name = f"Rim_{'L' if side>0 else 'R'}"
    rim.data.materials.append(mat_rim_metal)
    
    # 轮毂发光核心
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r * 0.35, depth=wheel_w * 1.12,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    hub_glow = bpy.context.active_object
    hub_glow.name = f"Hub_Glow_{'L' if side>0 else 'R'}"
    hub_glow.data.materials.append(mat_glow_cyan)

print("✅ High-Quality Cyberpunk Companion Robot 3D Shell Built Successfully!")
"""

# 连接并发送至 Blender 执行
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 9876))
cmd = {"type": "execute_code", "params": {"code": blender_script}}
s.sendall(json.dumps(cmd).encode("utf-8"))
data = s.recv(16384)
print("Execute response:", data.decode("utf-8"))
s.close()
