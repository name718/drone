#!/usr/bin/env python3
"""
桌面机器人核心电子元器件 3D 占位模型库 (Mockup Components)
生成标准 1:1 尺寸的元器件实体模型 (带真实轮廓与安装耳片)
可导入 Shapr3D 与底盘骨架共同作为外观造型的空间参考基准
"""

import os
import cadquery as cq

out_dir = os.path.dirname(os.path.abspath(__file__))

print("正在生成 1:1 元器件装配占位模型...")

# 1. 左右 N20 减速电机与标准轮子占位体
motor_assembly = cq.Assembly()

for is_left in [True, False]:
    sign = 1.0 if is_left else -1.0
    name_suffix = "left" if is_left else "right"
    y_pos = (78.0 / 2.0 - 10.0) * sign
    
    # 电机本体 (12 x 10 x 24 mm)
    motor_box = (
        cq.Workplane("XY")
        .box(25.0, 12.0, 10.0)
        .translate((5.0, y_pos, 8.8))
    )
    # 3mm D轴
    shaft = (
        cq.Workplane("XZ")
        .circle(1.5)
        .extrude(9.0 * sign)
        .translate((8.0, y_pos, 8.8))
    )
    # 橡胶小轮胎 (直径 43mm, 宽 18mm)
    wheel = (
        cq.Workplane("XZ")
        .circle(43.0 / 2.0)
        .extrude(18.0 * sign)
        .translate((8.0, (78.0 / 2.0 + 1.0) * sign, 8.8))
    )
    
    motor_assembly.add(motor_box, name=f"n20_motor_{name_suffix}", color=cq.Color(0.7, 0.7, 0.7, 0.8))
    motor_assembly.add(shaft, name=f"n20_shaft_{name_suffix}", color=cq.Color(0.8, 0.8, 0.2, 1.0))
    motor_assembly.add(wheel, name=f"wheel_{name_suffix}", color=cq.Color(0.2, 0.2, 0.2, 0.9))

# 2. 3S 锂电池 (标称 60 x 30 x 20 mm)
battery = (
    cq.Workplane("XY")
    .box(60.0, 30.0, 20.0)
    .edges()
    .fillet(1.5)
    .translate((-5.0, 0.0, 13.6))
)
motor_assembly.add(battery, name="battery_3s", color=cq.Color(0.2, 0.5, 0.9, 0.7))

# 3. ST7735 1.8寸 TFT 彩屏 (PCB 34 x 58 mm, 屏幕 28 x 35 mm)
lcd_pcb = (
    cq.Workplane("XY")
    .box(58.0, 34.0, 1.6)
    .translate((0.0, 0.0, 50.0))
)
lcd_glass = (
    cq.Workplane("XY")
    .box(35.0, 28.0, 2.4)
    .translate((0.0, 0.0, 52.0))
)
motor_assembly.add(lcd_pcb, name="lcd_pcb", color=cq.Color(0.1, 0.2, 0.5, 0.9))
motor_assembly.add(lcd_glass, name="lcd_glass", color=cq.Color(0.0, 0.8, 0.9, 0.6))

# 4. 双轴 SG90 舵机云台简易占位体
sg90_pan = (
    cq.Workplane("XY")
    .box(23.0, 12.2, 29.0)
    .translate((0.0, 0.0, 32.0))
)
motor_assembly.add(sg90_pan, name="sg90_pan", color=cq.Color(0.1, 0.4, 0.8, 0.7))

# 导出装配体 STEP 文件
mockup_step_path = os.path.join(out_dir, "drone_mockup_components.step")
print(f"正在导出元器件参考占位体: {mockup_step_path} ...")
motor_assembly.save(mockup_step_path)

print("装配体参考模型导出完成！")
