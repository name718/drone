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

# 2. 材质库定义 (包含半透明透视外壳 + 真实电子器件颜色)
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

# 外壳材质 (半透明太空银白，方便 X-Ray 透视内部空间)
mat_xray_shell = get_mat("XRay_Shell_White", (0.92, 0.94, 0.98, 0.35), roughness=0.1, alpha=0.35)
mat_xray_visor = get_mat("XRay_Visor", (0.05, 0.1, 0.2, 0.4), roughness=0.05, alpha=0.4)
mat_rubber     = get_mat("Tire_Rubber", (0.04, 0.04, 0.04, 1.0), roughness=0.85)
mat_rim_metal  = get_mat("Wheel_Rim_Metal", (0.85, 0.85, 0.88, 1.0), roughness=0.2, metallic=0.8)

# 内部真实电子元器件材质
mat_bat_blue   = get_mat("18650_LiIon", (0.1, 0.45, 0.95, 1.0), roughness=0.2)
mat_n20_brass  = get_mat("N20_Gearbox", (0.85, 0.68, 0.22, 1.0), metallic=0.85, roughness=0.2)
mat_n20_silver = get_mat("N20_Motor_Body", (0.8, 0.8, 0.82, 1.0), metallic=0.9, roughness=0.2)
mat_pcb_stm32  = get_mat("PCB_STM32_Green", (0.05, 0.45, 0.15, 1.0), roughness=0.3)
mat_pcb_esp32  = get_mat("PCB_ESP32_Black", (0.08, 0.08, 0.09, 1.0), roughness=0.3)
mat_driver_red = get_mat("TB6612_Driver_Red", (0.85, 0.1, 0.1, 1.0), roughness=0.3)
mat_ic_black   = get_mat("IC_Chip_Black", (0.02, 0.02, 0.02, 1.0), roughness=0.4)
mat_oled_glow  = get_mat("OLED_Screen_Cyan", (0.0, 0.85, 1.0, 1.0), emission=True, emit_strength=8.0)

# ==============================================================
# 真实工业级空间尺寸 (针对 65mm 长 18650 电池优化机身空间)
# ==============================================================
wheel_r   = 0.0215  # 43mm 轮径 (半径 21.5mm)
wheel_w   = 0.014   # 14mm 轮宽
chassis_w = 0.052   # 机身外宽 52mm (内部净宽 46mm)
chassis_l = 0.074   # 机身外长 74mm (内部净长 68mm，完美容纳 65mm 长的 18650 电池！)
chassis_h = 0.088   # 机身总高 88mm

# 3. 左右车轮
wheel_y = (chassis_w / 2.0) + 0.004 + (wheel_w / 2.0)
for side in [-1, 1]:
    y_pos = side * wheel_y
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r, depth=wheel_w,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    tire = bpy.context.active_object
    tire.name = f"Tire_{'L' if side>0 else 'R'}"
    tire.data.materials.append(mat_rubber)
    
    bpy.ops.mesh.primitive_cylinder_add(
        radius=wheel_r * 0.76, depth=wheel_w * 1.05,
        location=(0, y_pos, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    rim = bpy.context.active_object
    rim.name = f"Rim_{'L' if side>0 else 'R'}"
    rim.data.materials.append(mat_rim_metal)

# ==============================================================
# 4. 内部核心器件精准排布 (从底到顶严丝合缝)
# ==============================================================

# ① 底层：2颗 N20 减速电机 (宽12mm x 高10mm x 长24mm)
for side in [-1, 1]:
    # 减速箱
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(0, side * 0.018, wheel_r)
    )
    gb = bpy.context.active_object
    gb.name = f"N20_Gearbox_{'L' if side>0 else 'R'}"
    gb.scale = (0.012, 0.009, 0.010)
    gb.data.materials.append(mat_n20_brass)
    
    # 电机马达体
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.006, depth=0.015,
        location=(0, side * 0.006, wheel_r), rotation=(math.pi/2, 0, 0)
    )
    m_body = bpy.context.active_object
    m_body.name = f"N20_Motor_{'L' if side>0 else 'R'}"
    m_body.data.materials.append(mat_n20_silver)

# ② 底层电机上方：TB6612 电机驱动小板 (红色，18x18mm)
z_driver = wheel_r + 0.009
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.015, 0, z_driver))
driver = bpy.context.active_object
driver.name = "TB6612_Driver_Board"
driver.scale = (0.018, 0.018, 0.002)
driver.data.materials.append(mat_driver_red)

# ③ 中层：2节 18650 动力锂电池 (长65mm x 直径18.2mm，沿 X 轴前后并排)
z_bat = z_driver + 0.014
for b_idx in [-1, 1]:
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.0091, depth=0.065, # 直径18.2mm, 长度65mm 真实规格
        location=(0, b_idx * 0.0105, z_bat), rotation=(0, math.pi/2, 0)
    )
    bat = bpy.context.active_object
    bat.name = f"18650_Battery_Cell_{b_idx}"
    bat.data.materials.append(mat_bat_blue)

# ④ 顶层：你的 STM32F401 主控板 (绿色 PCB 36x36mm x 1.6mm)
z_stm32 = z_bat + 0.018
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-0.005, 0, z_stm32))
stm32_pcb = bpy.context.active_object
stm32_pcb.name = "STM32F401_Mainboard_PCB"
stm32_pcb.scale = (0.036, 0.036, 0.0016)
stm32_pcb.data.materials.append(mat_pcb_stm32)

# 主控芯片 STM32 + IMU
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-0.005, 0, z_stm32 + 0.0015))
mcu_chip = bpy.context.active_object
mcu_chip.name = "STM32F401_MCU_Chip"
mcu_chip.scale = (0.007, 0.007, 0.001)
mcu_chip.data.materials.append(mat_ic_black)

# ⑤ 头部前方：ESP32 智能上位机模块 (黑色微型核心板 22x18mm)
z_esp32 = z_stm32 + 0.018
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.012, 0, z_esp32))
esp32_pcb = bpy.context.active_object
esp32_pcb.name = "ESP32_AI_Module_PCB"
esp32_pcb.scale = (0.022, 0.018, 0.0016)
esp32_pcb.data.materials.append(mat_pcb_esp32)

# ⑥ 头部面罩正后方：0.96寸 OLED 发光表情屏 (27x27mm)
z_oled = z_esp32 + 0.006
bpy.ops.mesh.primitive_cube_add(
    size=1.0, location=(0.028, 0, z_oled), rotation=(0, -math.radians(10), 0)
)
oled = bpy.context.active_object
oled.name = "OLED_0.96_Display"
oled.scale = (0.003, 0.027, 0.027)
oled.data.materials.append(mat_oled_glow)

# ==============================================================
# 5. 半透明机甲外壳 (X-Ray 透视观察内部余量)
# ==============================================================

# 躯干外壳 (长74mm x 宽52mm x 高48mm)
z_torso = z_bat + 0.004
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, z_torso))
torso_shell = bpy.context.active_object
torso_shell.name = "Torso_XRay_Armor"
torso_shell.scale = (chassis_l, chassis_w, 0.048)
torso_shell.data.materials.append(mat_xray_shell)
sub_t = torso_shell.modifiers.new(name="Bevel", type='BEVEL')
sub_t.width = 0.12
sub_t.segments = 4

# 头部头盔外壳
z_head = z_oled + 0.002
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.005, 0, z_head))
head_shell = bpy.context.active_object
head_shell.name = "Head_XRay_Helmet"
head_shell.scale = (0.056, 0.052, 0.038)
head_shell.data.materials.append(mat_xray_shell)
sub_h = head_shell.modifiers.new(name="Bevel", type='BEVEL')
sub_h.width = 0.18
sub_h.segments = 5

# 头部面罩
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.032, 0, z_head))
visor_shell = bpy.context.active_object
visor_shell.name = "Visor_XRay_Glass"
visor_shell.scale = (0.004, 0.044, 0.026)
visor_shell.data.materials.append(mat_xray_visor)

for obj in bpy.data.objects:
    if obj.type == "MESH":
        for poly in obj.data.polygons:
            poly.use_smooth = True

print("✅ X-Ray Internal Space Verification Model Built Successfully!")
"""

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 9876))
cmd = {"type": "execute_code", "params": {"code": blender_script}}
s.sendall(json.dumps(cmd).encode("utf-8"))
data = s.recv(16384)
print("Execute response:", data.decode("utf-8"))
s.close()
