// ====================================================================
// 桌面双轮自平衡机器人 3D 打印参数化建模源码 (OpenSCAD)
// 适配: 双 N20 减速电机 + 18650 电池盒 + 30.5x30.5mm STM32 主控板
// ====================================================================

$fn = 60;

// 全局尺寸定义 (单位: mm)
chassis_width = 75;    // 车身宽度
chassis_depth = 46;    // 车身厚度
plate_thick   = 3.0;   // 结构板厚度
pcb_hole_pitch= 30.5;  // 主控板孔距 (30.5x30.5mm)

// 1. 底层动力电机板 (Base Motor Plate)
module base_motor_plate() {
    difference() {
        // 主板体 (圆角矩形)
        hull() {
            translate([-chassis_width/2 + 5, -chassis_depth/2 + 5, 0]) cylinder(r=5, h=plate_thick);
            translate([ chassis_width/2 - 5, -chassis_depth/2 + 5, 0]) cylinder(r=5, h=plate_thick);
            translate([-chassis_width/2 + 5,  chassis_depth/2 - 5, 0]) cylinder(r=5, h=plate_thick);
            translate([ chassis_width/2 - 5,  chassis_depth/2 - 5, 0]) cylinder(r=5, h=plate_thick);
        }
        
        // 4角 M3 铜柱安装孔 (孔距: 63mm x 34mm)
        translate([-63/2, -34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ 63/2, -34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([-63/2,  34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ 63/2,  34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        
        // 左右 N20 电机支架安装孔 (M2)
        translate([-26, -9, -1]) cylinder(d=2.2, h=plate_thick+2);
        translate([-26,  9, -1]) cylinder(d=2.2, h=plate_thick+2);
        translate([ 26, -9, -1]) cylinder(d=2.2, h=plate_thick+2);
        translate([ 26,  9, -1]) cylinder(d=2.2, h=plate_thick+2);
        
        // 中间减重与过线槽
        hull() {
            translate([-10, -8, -1]) cylinder(r=3, h=plate_thick+2);
            translate([ 10, -8, -1]) cylinder(r=3, h=plate_thick+2);
            translate([-10,  8, -1]) cylinder(r=3, h=plate_thick+2);
            translate([ 10,  8, -1]) cylinder(r=3, h=plate_thick+2);
        }
    }
}

// 2. 顶层主控安装板 (Top Controller Plate with 30.5x30.5mm mount)
module top_controller_plate() {
    difference() {
        hull() {
            translate([-chassis_width/2 + 5, -chassis_depth/2 + 5, 0]) cylinder(r=5, h=plate_thick);
            translate([ chassis_width/2 - 5, -chassis_depth/2 + 5, 0]) cylinder(r=5, h=plate_thick);
            translate([-chassis_width/2 + 5,  chassis_depth/2 - 5, 0]) cylinder(r=5, h=plate_thick);
            translate([ chassis_width/2 - 5,  chassis_depth/2 - 5, 0]) cylinder(r=5, h=plate_thick);
        }
        
        // 4角 M3 铜柱安装孔
        translate([-63/2, -34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ 63/2, -34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([-63/2,  34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ 63/2,  34/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        
        // 中间主控板 30.5x30.5mm 安装孔
        translate([-pcb_hole_pitch/2, -pcb_hole_pitch/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ pcb_hole_pitch/2, -pcb_hole_pitch/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([-pcb_hole_pitch/2,  pcb_hole_pitch/2, -1]) cylinder(d=3.2, h=plate_thick+2);
        translate([ pcb_hole_pitch/2,  pcb_hole_pitch/2, -1]) cylinder(d=3.2, h=plate_thick+2);
    }
}

// 默认渲染底层板 (在 OpenSCAD 中可切换渲染或导出 STL)
base_motor_plate();
// translate([0, 60, 0]) top_controller_plate();
