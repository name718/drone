#!/usr/bin/env python3
"""
参数化桌面智能伴侣机器人 - 底盘动力核心骨架 (Chassis Core)
基于 CadQuery / OpenCASCADE 工业级几何内核构建
可直接导出供 Shapr3D 编辑的无损 STEP (.step) 与 3D 打印切片使用的 STL (.stl) 格式
"""

import os
import cadquery as cq

# ==============================================================================
# 一、 核心参数定义 (单位: 毫米 mm)
# ==============================================================================

# 1. 基础外形尺寸
CHASSIS_LENGTH = 100.0   # 整车底盘前后长度
CHASSIS_WIDTH  = 78.0    # 整车底盘左右宽度
BASE_THICKNESS = 3.6     # 主底板结构壁厚
CORNER_RADIUS  = 6.0     # 外壳四角平滑圆角

# 2. N20 减速电机安装槽参数 (左右对称)
N20_SLOT_WIDTH  = 12.4   # 电机宽度配合公差 (标准 12.0mm + 0.4mm 间隙)
N20_SLOT_HEIGHT = 10.4   # 电机厚度配合公差 (标准 10.0mm + 0.4mm 间隙)
N20_SLOT_LENGTH = 26.0   # 电机+减速箱卡入槽长度
N20_WALL_THICK  = 2.8    # 电机包胶固定壁厚
MOTOR_SHAFT_HOLE = 8.0   # 轮轴穿孔直径 (避让 3mm D轴与轮毂凸台)
WHEEL_TRACK     = 88.0   # 左右两轮中心轮距基准

# 3. 3S 锂电池下沉式中心收纳仓 (超低重心设计)
BATTERY_POCKET_L = 62.0  # 3S 电池长度容差
BATTERY_POCKET_W = 34.0  # 3S 电池宽度容差
BATTERY_RIB_H    = 10.0  # 电池侧面限位挡墙高度
VELCRO_SLOT_W    = 22.0  # 魔术贴扎带通孔宽度
VELCRO_SLOT_H    = 3.2   # 魔术贴扎带通孔厚度

# 4. 尾部辅助万向轮 / 支撑座参数
CASTER_OFFSET_X  = -38.0 # 支撑轮距离车身中心前后距离
CASTER_MOUNT_PITCH = 16.0# 支撑轮固定孔间距
CASTER_HOLE_DIA  = 3.2   # M3 螺丝安装通孔

# 5. 上层电气安装立柱参数 (配合 M2.5 / M3 热熔铜螺母)
PILLAR_OUTER_DIA = 6.2   # 螺母柱外径
PILLAR_HOLE_DIA  = 3.2   # 热熔螺母预留底孔 (适用于 M2.5 / M3 预埋螺母)
PILLAR_HEIGHT    = 12.0  # 支撑立柱高度
PILLAR_SPAN_X    = 70.0  # 前后立柱间距
PILLAR_SPAN_Y    = 60.0  # 左右立柱间距

# ==============================================================================
# 二、 参数化几何造型构建
# ==============================================================================

print("正在构建底盘主承载底板...")
# 1. 基础底板实体
base_plate = (
    cq.Workplane("XY")
    .box(CHASSIS_LENGTH, CHASSIS_WIDTH, BASE_THICKNESS)
    .edges("|Z")
    .fillet(CORNER_RADIUS)
)

print("正在开设中央走线通道与魔术贴扎带穿孔...")
# 2. 中间大走线孔 (倒圆角避让线束磨损)
wire_pass_hole = (
    cq.Workplane("XY")
    .slot2D(28.0, 12.0, 0.0)
    .extrude(BASE_THICKNESS + 2.0)
    .translate((12.0, 0.0, -1.0))
)
base_plate = base_plate.cut(wire_pass_hole)

# 3. 电池魔术贴扎带槽 (前后各一组穿槽)
for y_offset in [-14.0, 14.0]:
    velcro_cut = (
        cq.Workplane("XY")
        .box(VELCRO_SLOT_H, VELCRO_SLOT_W, BASE_THICKNESS + 2.0)
        .translate((0.0, y_offset, 0.0))
    )
    base_plate = base_plate.cut(velcro_cut)

print("正在构建左右 N20 双电机高精度一体化固定座...")
# 4. 左右对称 N20 电机支架卡座
motor_mount_x = 5.0
motor_mount_y_left = (CHASSIS_WIDTH / 2.0) - (N20_SLOT_WIDTH / 2.0) - N20_WALL_THICK
motor_mount_y_right = -motor_mount_y_left

for is_left in [True, False]:
    sign = 1.0 if is_left else -1.0
    y_pos = (CHASSIS_WIDTH / 2.0 - (N20_SLOT_WIDTH + 2 * N20_WALL_THICK) / 2.0) * sign
    
    # 电机外部凸台实体
    mount_block = (
        cq.Workplane("XY")
        .box(N20_SLOT_LENGTH + 4.0, N20_SLOT_WIDTH + 2 * N20_WALL_THICK, N20_SLOT_HEIGHT + 3.0)
        .translate((motor_mount_x, y_pos, (N20_SLOT_HEIGHT + 3.0) / 2.0 + BASE_THICKNESS / 2.0))
    )
    base_plate = base_plate.union(mount_block)

    # 掏空 N20 电机内部凹槽
    slot_cut = (
        cq.Workplane("XY")
        .box(N20_SLOT_LENGTH + 5.0, N20_SLOT_WIDTH, N20_SLOT_HEIGHT)
        .translate((motor_mount_x, y_pos, N20_SLOT_HEIGHT / 2.0 + BASE_THICKNESS / 2.0 + 1.0))
    )
    base_plate = base_plate.cut(slot_cut)

    # 侧壁外侧电机轴与轮毂避让孔
    shaft_cut = (
        cq.Workplane("XZ")
        .circle(MOTOR_SHAFT_HOLE / 2.0)
        .extrude(10.0 * sign)
        .translate((motor_mount_x + 3.0, (CHASSIS_WIDTH / 2.0 - 5.0) * sign, N20_SLOT_HEIGHT / 2.0 + BASE_THICKNESS / 2.0))
    )
    base_plate = base_plate.cut(shaft_cut)

    # N20 金属压板紧固螺丝固定孔 (M2)
    for screw_x in [motor_mount_x - 8.0, motor_mount_x + 8.0]:
        screw_hole = (
            cq.Workplane("XY")
            .circle(1.1)  # M2 螺丝过孔
            .extrude(N20_SLOT_HEIGHT + BASE_THICKNESS + 6.0)
            .translate((screw_x, y_pos, -2.0))
        )
        base_plate = base_plate.cut(screw_hole)

print("正在构建电池防滚限位肋骨...")
# 5. 电池仓四周环绕防晃动凸台
battery_cradle = (
    cq.Workplane("XY")
    .rect(BATTERY_POCKET_L + 3.0, BATTERY_POCKET_W + 3.0)
    .extrude(BATTERY_RIB_H)
    .translate((-5.0, 0.0, BASE_THICKNESS / 2.0))
)
battery_inner_cut = (
    cq.Workplane("XY")
    .rect(BATTERY_POCKET_L, BATTERY_POCKET_W)
    .extrude(BATTERY_RIB_H + 2.0)
    .translate((-5.0, 0.0, BASE_THICKNESS / 2.0))
)
battery_ribs = battery_cradle.cut(battery_inner_cut)
base_plate = base_plate.union(battery_ribs)

print("正在构建尾部辅助万向轮支撑结构...")
# 6. 尾部万向滚珠支架加厚台阶与安装孔
caster_boss = (
    cq.Workplane("XY")
    .box(18.0, 28.0, 4.0)
    .edges("|Z")
    .fillet(3.0)
    .translate((CASTER_OFFSET_X, 0.0, BASE_THICKNESS / 2.0 + 2.0))
)
base_plate = base_plate.union(caster_boss)

# 万向轮左右固定孔
for y_h in [-CASTER_MOUNT_PITCH / 2.0, CASTER_MOUNT_PITCH / 2.0]:
    c_hole = (
        cq.Workplane("XY")
        .circle(CASTER_HOLE_DIA / 2.0)
        .extrude(BASE_THICKNESS + 8.0)
        .translate((CASTER_OFFSET_X, y_h, -2.0))
    )
    base_plate = base_plate.cut(c_hole)

print("正在构建上层机壳堆叠固定安装铜柱...")
# 7. 四角上层机壳安装立柱 (带热熔螺母预留孔)
for px in [-PILLAR_SPAN_X / 2.0, PILLAR_SPAN_X / 2.0]:
    for py in [-PILLAR_SPAN_Y / 2.0, PILLAR_SPAN_Y / 2.0]:
        # 避开电机座区域做适度微调
        adj_px = px + 4.0 if px > 0 else px - 2.0
        pillar = (
            cq.Workplane("XY")
            .circle(PILLAR_OUTER_DIA / 2.0)
            .extrude(PILLAR_HEIGHT)
            .translate((adj_px, py, BASE_THICKNESS / 2.0))
        )
        pillar_hole = (
            cq.Workplane("XY")
            .circle(PILLAR_HOLE_DIA / 2.0)
            .extrude(PILLAR_HEIGHT + 2.0)
            .translate((adj_px, py, BASE_THICKNESS / 2.0 + 2.0))
        )
        base_plate = base_plate.union(pillar).cut(pillar_hole)

# ==============================================================================
# 三、 导出 STEP 与 STL 文件
# ==============================================================================

out_dir = os.path.dirname(os.path.abspath(__file__))
step_path = os.path.join(out_dir, "drone_chassis_core.step")
stl_path  = os.path.join(out_dir, "drone_chassis_core.stl")

print(f"正在导出高精度 STEP 文件: {step_path} ...")
cq.exporters.export(base_plate, step_path)

print(f"正在导出 3D 打印 STL 文件: {stl_path} ...")
cq.exporters.export(base_plate, stl_path)

print("==================================================================")
print("底盘机芯核心骨架 3D 模型生成完成！")
print(f"1. STEP 文件 (导入 Shapr3D 无损编辑): {step_path}")
print(f"2. STL 文件  (切片软件直接预览/打印): {stl_path}")
print("==================================================================")
