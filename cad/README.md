# 桌面智能伴侣机器人 - 3D 打印与 CAD 结构工程

本目录包含机器人的**参数化 Code-to-CAD 源码**与导出的**工业级 3D 模型文件**。

---

## 一、 文件清单

| 文件名 | 格式 | 用途说明 |
| :--- | :--- | :--- |
| [`drone_chassis_core.step`](file:///Users/mjt/Desktop/workplace/drone/cad/drone_chassis_core.step) | STEP (AP214) | **底盘机芯核心骨架**（无损 NURBS 几何曲面，直接导入 Shapr3D 编辑外观与倒角） |
| [`drone_chassis_core.stl`](file:///Users/mjt/Desktop/workplace/drone/cad/drone_chassis_core.stl) | STL 三角网格 | 3D 打印切片文件（直接拖入 Bambu Studio / OrcaSlicer / PrusaSlicer） |
| [`drone_mockup_components.step`](file:///Users/mjt/Desktop/workplace/drone/cad/drone_mockup_components.step) | STEP 装配体 | **1:1 内部元器件参考占位体**（包含左右 N20 电机、车轮、3S 电池、1.8寸彩屏、云台舵机），导入 Shapr3D 作为空间参考 |
| [`generate_chassis.py`](file:///Users/mjt/Desktop/workplace/drone/cad/generate_chassis.py) | Python (CadQuery) | 底盘骨架参数化生成源码（修改尺寸后重新运行一键生成新模型） |
| [`generate_mockups.py`](file:///Users/mjt/Desktop/workplace/drone/cad/generate_mockups.py) | Python (CadQuery) | 元器件参考体生成源码 |

---

## 二、 Shapr3D 导入与造型工作流

1. **导入工程基准**：
   - 打开 Shapr3D，点击 **导入 (Import)**；
   - 先导入 [`drone_chassis_core.step`](file:///Users/mjt/Desktop/workplace/drone/cad/drone_chassis_core.step) 作为实体零件；
   - 再导入 [`drone_mockup_components.step`](file:///Users/mjt/Desktop/workplace/drone/cad/drone_mockup_components.step) 作为空间参考；
   - 此时你会看到一个完整的底盘机芯，所有关键电机槽、电池仓、螺丝孔位已精确就位。
2. **在 Shapr3D 中做个性化造型**：
   - **外壳曲面包裹**：选定四角立柱和底盘外沿，拉伸出外壳侧板，利用 Shapr3D 的曲面与放样功能进行圆润化或科幻装甲切面造型；
   - **头部/面罩设计**：围绕已有的 1.8 寸屏幕占位体设计面罩视窗与卡槽；
   - **声学与开关开孔**：在侧壁开出 Type-C 烧录口、电源开关卡槽与喇叭出音孔阵列。

---

## 三、 3D 打印切片推荐设置 (FDM)

- **推荐材质**：PLA+ 或 PETG
- **层高 (Layer Height)**：0.20mm (标准) 或 0.16mm (精细)
- **壁厚圈数 (Wall Loops)**：3~4 圈（保证螺丝紧固强度）
- **填充率 (Infill)**：20%~25%（推荐陀螺仪形 Gyroid 填充，受力均匀）
- **支撑 (Supports)**：开启树状支撑（Tree Supports），仅在电机轴穿孔悬空处生成
