# C8T6 头部固件：屏幕、舵机及串口协议

本工程把原 ESP32 的 ST7735 表情和双轴云台迁移到实测 **64KB Flash / 20KB RAM** 的 STM32F1 小蓝板。日期：2026-10-18。

## 先了解三块板如何协作

```text
ESP32 的 DisplayService / GimbalService
        │ 只发送表情、音量、目标角度和手势
        │ UART2，GPIO9 TX / GPIO18 RX，115200 8N1
        ▼
Luckfox /dev/ttyS3 ← head_bridge.py → /dev/ttyS4
        │ UART4
        ▼
C8 USART1，PA9 TX / PA10 RX
        ├── SPI1 → ST7735，128×160，保留原MADCTL=0xC8
        ├── TIM2 → 两个舵机，硬件50Hz PWM
        └── PB11/PB12 → 摄像头RESET/PWDN
```

Luckfox桥接程序只处理本次头部协议，不负责图像推理，不转发JPEG到C8。摄像头DVP采集与ESP网页预览已按下文完成短时实测；ESP32到Luckfox的图像传输和推理尚未实现。

## 当前验证边界

- C8固件已实际烧录，st-flash写后校验通过；原64KB固件已备份到 `artifacts/c8_before_migration_20261018.bin`。
- 主机ASan/UBSan测试覆盖CRC标准向量、乱流重同步、损坏帧、长度、超时、计时回绕、手势去重、限位和全部表情绘制。差量刷新另用模拟ST7735显存逐像素核对1200帧动画，覆盖旧画面擦除、缓存淘汰、传输中断后恢复；蚊香眼旋转与原公式在1000个角度比较，量化偏移不超过1像素。
- Python桥接程序用两个真实PTY和独立进程验证双向传输、分片、坏CRC过滤和未知类型隔离。
- 迁移版C8固件通过28项无运动SWD回归，确认时钟、DMA刷新、表情、协议、摄像头控制脚和PWM关闭状态；报告为`artifacts/hardware_test.json`。该报告及下述33项UART报告对应降速前镜像；降速后重新执行的三板测试见`artifacts/head_end_to_end_test.json`。
- Luckfox Mini B已实际刷入官方Buildroot Flash镜像，Python为3.11.6；UART3_M1、UART4_M1已启用，桥已部署且设为开机启动。
- **真实UART4↔C8已通过33项检查**，报告为`artifacts/luckfox_uart_direct.json`；包含12种表情、非法命令、坏CRC、重同步、心跳超时，全程不使能舵机，UART硬件错误增量为0。
- **PTY→真实桥进程→UART4→C8又通过33项检查**，报告为`artifacts/luckfox_uart_bridge.json`。其中PTY模拟ESP端，不能当成ESP实物和UART3接线已通过。
- 实机服务通过7项生命周期检查：重复启动、异常子进程重启、停止清理、真实系统重启后自动启动、最终持有两路UART。报告为`artifacts/luckfox_service_test.json`。重启后UART4接收及UART3发送各增加220字节/秒，证明服务持续转发C8状态。
- ESP32-S3代理最终版已实际烧录并校验，16MB Flash / 8MB PSRAM与配置一致，启动日志无异常，网页遥测可用。原来会覆盖的全部Flash范围`0x0..0x160fff`已备份（不是整片16MB备份），路径为`../firmware_brain/artifacts/esp32_before_head_migration_written_range_20261018.bin`。921600/460800读取不稳定，115200读取和烧录均通过校验。
- **真实三板全链路13项检查全部通过**：电脑WebSocket→ESP32→UART2→Luckfox UART3/4→C8，C8状态再原路返回ESP遥测；报告为`artifacts/head_end_to_end_test.json`。SLEEPY、SURPRISED表情在C8实际更新，30°/1°目标由C8执行并回传计算角度，最后恢复NORMAL、29°/0°。此轮UART/CRC错误计数没有新增，SPI错误和HardFault为0。
- 还通过了真实单向故障回归：测试程序丢弃ESP命令、继续回传C8状态，ESP正确判定控制离线，C8超时停PWM；恢复正式桥后自动恢复在线。`HeadLink::online()`采用“回传新鲜且C8确认收到STATE心跳”，并记录在线/离线变化。源码、编译镜像、已烧入镜像哈希一致，见`artifacts/esp32_flash_20261018.json`。
- SPI写入没有屏幕读回线，DMA成功不能证明屏幕物理连接和显示颜色正确。用户已确认低速软件SPI三色条和1.125MHz硬件SPI表情可见，固化版本烧录重启后也确认表情正常；最终差量刷新与旋转优化版再次确认“正常，明显顺畅”。舵机报告的是PWM对应的计算角度，不是位置传感器反馈，机械动作仍需目视验收。
- 独立C8回归测试结束时PWM关闭；三板在线测试结束后，ESP继续发控制心跳，C8保持NORMAL及29°/0°且软件PWM使能，自主动作关闭。测试时舵机动力电源保持断开，因此未验证舵机机械实际位置。上述头部测试时摄像头保持休眠/复位；当前摄像头服务会唤醒采集。开发板PC13指示灯每秒亮100ms表示有有效STATE心跳，亮500ms表示离线。

判断通信测试结果应比较初末错误计数增量，不能把历史累计数当作本轮新增错误。早期接线排查曾产生UART错误，协议回归也会故意注入损坏帧；每次报告保存本轮基线及结束值。

最终C8镜像占用Flash 18232字节，静态RAM 14856字节，另预留4KB栈。SPI固定为1.125MHz，原先逐帧整屏刷新约307ms；现采用4×4块差量刷新，常用表情可接近20帧/秒。详细实测数据见`artifacts/display_performance.json`。报告中的CRC错误和非法命令拒绝包含测试主动注入；UART硬件错误单独计数。UART实测发现并修复了BOOT动画播完后，同目标状态的新事件无法重播的问题；同event_id心跳仍不会反复重置动画。

最终版实板各表情连续测速3秒：NORMAL/HAPPY/SURPRISED/SLEEPY/LOVE/ANGRY/CONFUSED约19.6~20帧/秒，DIZZY约10.3帧/秒（首轮8×8差量版仅3.6帧/秒）。FPS来自帧计数增量，属于短时实测，不是所有输入下的最低帧率保证；复杂旋转、大面积切换仍受1.125MHz带宽限制。主机1200帧逐像素验证覆盖全部12种表情；实板测速仅覆盖网页接口支持的上述8种。

`artifacts/emotions_preview.png` 是共用算法在电脑上生成的12种表情预览，**不是实屏照片**。顺序为：

```text
BOOTING   NORMAL     HAPPY   SURPRISED
SLEEPY    LOVE       ANGRY   CONFUSED
DIZZY     THINKING   WINK    COOL
```

## 接线对应关系

### C8T6

| 功能 | 引脚 |
| --- | --- |
| 屏幕SCLK / SDA(MOSI) / CS | PA5 / PA7 / PA4 |
| 屏幕DC / RST | PB0 / PB1 |
| 屏幕BLK | 电源板3.3V，常亮；不接MCU GPIO |
| 水平 / 俯仰舵机信号 | PA0 / PA1 |
| 串口TX / RX | PA9 / PA10 |
| 摄像头RST / PWDN | PB11 / PB12 |
| ST-Link SWDIO / SWCLK | PA13 / PA14 |

表中的PAx在小蓝板上标作Ax。屏幕RST是B1，摄像头RST是B11。

### OV5640完整线序（恢复自前次最终接线方案）

2026-10-18从本对话当天12:15的最终“新供电板＋8pin ST7735”接线表逐项恢复，用户确认实物按该表连接。此前遗漏文档不代表用户尚未接线。按模组丝印找信号，不按排针左右位置猜；这套表中的D0就是蓝板标注的D0，不能照搬其他板的Y2～Y9编号。

| OV5640丝印 | 终点 | 作用 |
| --- | --- | --- |
| 3V3 | 鹿小班供电板3.3V | 模组电源 |
| GND | 公共GND | 电源和信号参考 |
| D0 | ESP32 GPIO1 | DVP bit 0 |
| D1 | ESP32 GPIO2 | DVP bit 1 |
| D2 | ESP32 GPIO10 | DVP bit 2 |
| D3 | ESP32 GPIO11 | DVP bit 3 |
| D4 | ESP32 GPIO12 | DVP bit 4 |
| D5 | ESP32 GPIO13 | DVP bit 5 |
| D6 | ESP32 GPIO14 | DVP bit 6 |
| D7 | ESP32 GPIO21 | DVP bit 7 |
| PCLK | ESP32 GPIO39 | 像素时钟输入 |
| VS / VSYNC | ESP32 GPIO40 | 帧同步输入 |
| HREF | ESP32 GPIO41 | 行有效输入 |
| SDA / SIOD | ESP32 GPIO42 | SCCB配置数据 |
| SCL / SIOC | ESP32 GPIO47 | SCCB配置时钟 |
| RST | C8 PB11（B11） | 摄像头复位，区别于屏幕B1 |
| PD / PWDN | C8 PB12（B12） | 摄像头休眠控制 |
| NC | 不接 | 不能当作XCLK |

原模组资料`/Users/mjt/Downloads/ZL0008/ESP32S3_OV5640_WiFi/README.md`注明板载24MHz晶振；配置`pin_xclk=-1`。驱动必须先等待C8 CAMERA_READY，再探测SCCB地址0x3c的0x300A/0x300B是否为0x5640，最后验证DVP帧；READY本身不能代替采图验证。ESP引脚定义已写入`../firmware_brain/main/config/board_config.hpp`的`Config::Camera`。

相机调试结果（2026-10-18）：当前默认使用VGA 640×480、JPEG质量20、内部PLL根分频开启、DVP约3MHz（默认档17）。补齐模拟前端初始化，保留SRAM分段DMA；直接PSRAM DMA在本机实测无法取得有效JPEG，已回退。60张JPEG、MJPEG与并发状态请求通过，单帧缓存，约3帧/秒；连续抽查的图像未见之前固定纵向条纹与异常色块。验收属于短时实测，不能扩展为所有照明和线长下的可靠性保证。

自动对焦：ESP启动先请求C8休眠并等待READY清除，再唤醒，避免只重启ESP却继承旧相机状态。只装载一次官方AF固件，再发单次对焦；重新对焦先释放旧任务，等ACK清零后再启动新搜索。实测最终0x3029=0x10、0x3023=0，表示传感器报告完成。`autofocus_ready`仅表示固件/命令初始化成功，`focused`才表示完成，网页分别显示，不能混用。

相关源码位于`../firmware_brain/main/camera/`，固定依赖`espressif/esp32-camera 2.1.8`。模拟前端寄存器初值对照[Linux官方OV5640驱动](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ov5640.c)；只补充寄存器配置数据，不替换官方ESP采集驱动。曾经只有JPEG文件头/少数解码帧通过而画面仍损坏，现已在主机报告中明确区分解码传输验收与人工画质检查。

控制HTTP 80端口：`/camera`预览页面，`/camera/status`报告阶段、PID、帧数、失败数、实际尺寸、AF原始状态和ACK；`POST /camera/retry`只在任务失败退出后重试；`POST /camera/focus`排队执行重新对焦。HTTP线程不直接操作SCCB。图像独立在81端口：`/capture`单张JPEG，`/stream`为MJPEG。超过1秒的旧帧不再发送，5秒没有新帧关闭流，页面自动重连。81端口仍串行处理，一次只保留一个视频客户端；80端口控制服务独立运行，LWIP连接总额度已提高到16。

`POST /camera/profile?id=0..20`是有限的诊断预设；默认17是目前通过色彩与连续采集测试的VGA档。19是历史低DVP时钟对照，15/18/20为内置彩条，其余是历史时钟/增益对照，不能理解为全部都可用。公开`sensor_t::set_pll`参数顺序为bypass/multiplier/sys/root/pre/seld5/pclken/pclk，与内部helper不同。最终使用multiplier80、sys4、root开启、pre2、seld5=2、DVP手动除4，保留自动曝光/增益与50Hz抗频闪设置。

验收工具`../tools/test_camera_capture.py`支持`--width 640 --height 480`；检查完整JPEG解码、帧号递增、MJPEG、并发状态请求及失败数增量，每10帧保存人工抽查样本。最终报告`../firmware_brain/artifacts/camera_slow_output_test.json`与`camera_final_reconnect_test.json`，只读头部遥测`camera_final_head_telemetry.json`（117次均在线，最大年龄198ms，未发动作命令）。前期失败报告保留，包括`camera_vga_test.json`、`camera_analog_test.json`、`camera_psram_dma_test.json`，不得把它们改写成通过。图像仍未接入Luckfox推理。

去条纹阶段镜像在用户反馈后继续修正色彩。去条纹/初步验色阶段镜像SHA256：`fb3beb43dbf04a6ae8702b2966f4d2fad3fb2e107e660d193fbd0631fb105734`（已被下述单缓存版本取代），esptool写后校验通过。复位后30帧验收亦通过（`camera_final_restart_test.json`），网页“重新对焦”按钮已点击并回读0x10/ACK0。浏览器实际预览截图`camera_final_preview.png`；后续较长预览中累计出现1次拒绝帧，画面继续更新，因此只声明固定条纹/对焦修复和短时验收通过，不宣称长期零丢帧。此前60帧测试期间无新增失败帧，两者测试区间不同。

色彩修复（2026-10-18）：用户实物验证发现蓝色偏绿，说明此前去条纹验收未覆盖色彩准确性。逐项恢复前端寄存器时，仅恢复0x3906模组原值可让八色彩条全部通过。此模组0x3906复位值实测0x78，复制参考表中的0x10会造成偏色；已从模拟前端补充表中删除该项，不再覆盖。保留其余前端、AF握手及SRAM DMA设置，默认档17；初步双缓存版本约5.92帧/秒，但后续被用户发现隔帧偏色，不能当作色彩稳定验收。不能把Linux参考表的全部值不加验证地照搬到此板。

最终色彩证据：`camera_color_analog_isolation.json`记录旧诊断固件中模式27（仅恢复0x3906）通过，其余逐项模式失败/错误；`camera_color_final_test.json`记录正式固件的白、黄、青、绿、紫、红、蓝、黑八色检测全部通过，0x3906=120(0x78)、0x503d=128(静止彩条)。滚动彩条模式会自带一条移动横线，不能将该线误判为采集损坏。`camera_color_final_stream_test.json`记录640×480连续60帧、MJPEG、控制并发、无新增失败帧；`camera_color_head_telemetry.json`记录117次头部遥测均在线，最大年龄200ms，未发动作命令。标准彩条验证内部颜色映射，真实物体的色准仍需结合现场照明和用户实物确认。

新增`POST /camera/color?mode=0..8`，0自动白平衡/正常色彩，1日光，2白光灯，3暖光，4单位增益对照，5静止彩条，6/7/8为翻转读出方向对照；HTTP只排队，SCCB仍由采集任务独占。正常启动默认0，不使用彩条。`GET /camera/registers`只返回固定白名单的寄存器快照，不提供任意寄存器写入。`../tools/test_camera_color.py --ip <IP> --output <报告路径>`按彩条主色判定并以退出码反映通过/失败，结束恢复调用前模式。临时的逐项恢复模式9..58已从正式固件清除。

色彩时间稳定性最终结果：用户在初步验色通过后报告“一会正常一会异常”。逐帧MJPEG记录发现，自动白平衡和固定白平衡各50帧都有49次明显色彩跳变，0x3906和JPEG头保持相同，不能归因于白平衡跳变；标准彩条本身不能暴露这一真实场景问题。切换`fb_count=1`后，连续60帧大幅色彩跳变0次；再重启后连续40帧亦0次、中性ROI未持续偏紫。实测约2.97~3.01帧/秒，低于双缓存版本，优先保证颜色稳定。实机对照证明此配置有效，尚未证明底层根因仅在驱动缓存，不能把推测写成硬件定论。

当前镜像：SHA256 `3eead7c4506a513a8d4fdd002fc73a171489f72da49077cbc09721bd2c3b0940`，1498160字节，esptool写后校验通过。用户已明确确认“颜色稳定，不再闪变”。最终报告`camera_single_buffer_color_stability.json`、`camera_single_buffer_restart_stability.json`、`camera_single_buffer_color_bars.json`（连续20帧八色全通过）、`camera_single_buffer_http_test.json`；头部只读遥测`camera_single_buffer_head_telemetry.json`（87次均在线，最大年龄199ms，无动作指令）。自动对焦最终0x10/ACK0、失败帧计数在上述测试区间保持0。初次烧录后的初始化曾失败一次，ESP复位恢复，后续再复位测试通过；这不构成所有上电条件下的启动保证。

新增`../tools/test_camera_color_stability.py`读取连续MJPEG，对固定ROI记录色度差，防止单张快照抽样恰好跳过每隔一帧的错误；`--neutral-roi`用于白/灰参照，另检查持续偏紫，不能对任意彩色移动物体使用该断言。更新后的`test_camera_color.py`同样连续读取20帧彩条，每一帧都必须通过。旧版单张验色通过和JPEG解码通过只证明各自的有限范围，不能扩大成连续真实场景颜色稳定。

供电现场记录：用户先报告发热，后明确为普通温热、可以持续触碰。电源板输出曾测3.3V，摄像头端早期3.0V；用短线直连后最终明确是在摄像头3V3/GND之间测得3.16V。这个数值不是两处地之间的电压，不能将其解释为地线存在3.16V压差，也不能据此认定条纹完全由供电造成。PCLK线及相机到ESP的短地线也已按对话更换/补接。完整供电带载余量尚未验收。

### 鹿小班供电板完整分配与当前调试状态

下表同样恢复自最终接线方案，输入为板上标注VIN:5V的Type-C 5V电源；其他输出电压不用。额定电流和满载压降尚未实测，下表记录分配，不等于供电带载已验收。

| 电源起点 | 终点 |
| --- | --- |
| +5V | ESP32 5V |
| +5V | Luckfox Mini B物理1脚VBUS |
| +5V | C8 5V |
| +5V | MAX98357A VIN |
| 3.3V | OV5640 3V3 |
| 3.3V | INMP441 VDD |
| 3.3V | ST7735 VCC |
| 3.3V | ST7735 BLK（常亮） |
| GND | ESP32、Luckfox 2或21脚、C8、相机、屏幕、麦克风、功放公共地 |
| GND | INMP441 L/R |
| LM2596 OUT− | 公共GND，舵机动力回流直接回LM2596 |
| LM2596 OUT+，5.0V | 两个舵机红线；不由逻辑供电板供动力 |
| LM2596 OUT− | 两个舵机棕/黑线 |

统一供电方案中各开发板3.3V输出留空，不与供电板3.3V并接；5V、3.3V电源轨分开。MAX98357A SPK+/SPK−各接喇叭一端，两端均不接公共地；GAIN和SD_MODE保留原方案的悬空配置。

实机调试阶段曾按对话操作切换为ESP/Luckfox各自USB、C8由ST-Link供电，并断开Luckfox外部供电线。恢复统一供电应先断电核对；不能把上述设计分配误记为调试时所有电源线均已恢复，也不能同时接多路供电输出。当前摄像头调试继续保持舵机动力断开，底盘不在本轮范围。

### 白屏排查与调试入口

本次接线在9MHz硬件SPI下出现纯白屏；补齐DMA结束后的TXE、BSY等待后仍白屏。保持同一组接线及初始化命令，低速软件SPI三色条和1.125MHz硬件SPI表情均经用户目视确认正常，随后4.5MHz测试又出现白屏。因此将默认SPI时钟固定为72MHz/64=1.125MHz，烧录复位后已读取SPI_CR1确认生效，用户也确认重启后表情正常。这将问题缩小到高速传输稳定性；没有示波器测量，不能确定是哪根线的波形或屏幕时序余量导致。最终固件重新通过13项真实三板通信检查；实屏目视结果及运行诊断见诊断报告。

`g_lcd_test_request`仅供SWD排查：写1切到低速软件SPI三色条，写2恢复硬件SPI和正常表情。切换由主循环在当前帧完成后执行，并重新初始化屏幕；`g_lcd_test_active`为1表示三色条模式，0表示正常表情。软件SPI整帧约3秒，保留串口中断并逐行处理心跳，不能用于评估正常动画速度。复位后默认进入硬件SPI表情模式。

现场诊断见`artifacts/tft_white_screen_diagnostic.json`。评估SPI速度时需要同时检查画面和通信错误计数；仅看到DMA完成不足以判定显示正确。

### Luckfox Mini

芯片面朝你，USB朝上，CAM座朝下：左排从上到下1~11，右排从下到上12~22。

| Luckfox物理针号 | 连接 |
| --- | --- |
| 13 UART3_RX | ESP32 GPIO9 TX |
| 12 UART3_TX | ESP32 GPIO18 RX |
| 11 UART4_TX | C8 PA10 RX |
| 10 UART4_RX | C8 PA9 TX |
| 2或21 GND | 公共GND |

串口信号电平为3.3V，TX/RX交叉。所有模块共地。舵机由独立5V动力支路供电，不让大电流通过开发板排针和面包板。新电源板的满载能力仍需实测；信号固件测试不等于供电余量验证。

## 学习代码的推荐顺序

1. `Inc/board.hpp`、`Src/board.cpp`：时钟、GPIO、SysTick、SPI DMA、串口中断、TIM2。先看一个外设的时钟使能，再看GPIO复用，最后看外设寄存器。
2. `../protocol/head_protocol.h`：为何显式序列化、CRC覆盖哪些字节、串口如何重新同步。
3. `Src/main.cpp`：主循环只负责调度，UART中断只搬字节；协议解析在主循环完成。
4. `../shared/ui/head_controller.cpp`：心跳、动作事件编号、定点平滑、机械限位。
5. `Src/head_display.cpp`：4位像素如何节省RAM，如何比较4×4块、合并窗口，再逐行展开为RGB565。
6. `../shared/ui/face_engine.cpp`：从ESP32原代码提取的同一份表情算法。原有几何、颜色、眨眼、呼吸、声浪得以保留。
7. `../firmware_brain/main/comm/head_link.cpp` 和 `../tools/luckfox/head_bridge.py`：ESP发送期望状态，Luckfox验证并转发，C8独立执行。

## 为什么不用整屏RGB565和FreeRTOS

128×160×2=40960字节，超过芯片全部20KB SRAM。本固件使用固定16色调色板，每像素4位：128×160÷2=10240字节。每次仅用256字节栈缓冲展开一个窗口的一行，通过DMA发给屏幕。

为了减少1.125MHz下的传输量，将画面分成1280个4×4块。纯色块只记一个颜色编号，128个非纯色块缓存保留精确的8字节像素；合计约2.5KB，不需要再保存整屏旧帧。比较的是绘制完成后的像素，因此每帧在RAM里clear重画不会让静止背景重复发送。相邻变化块合并窗口，静止块不占SPI带宽。

缓存不足时把被淘汰块标为未知，下次多发一次；不使用哈希，也不会因碰撞漏刷。窗口完整发送成功才更新缓存，传输中断则作废全部缓存，下次恢复完整画面。初始化、复位同样先作废缓存，确保第一帧完整写入。全屏内容突然变化仍需要约0.3秒，差量刷新不改变物理带宽上限。

旋转蚊香眼的固定螺旋点存放在`../shared/ui/dizzy_points.hpp`的Flash常量表中，每只眼每帧仅计算一次旋转角的sin/cos，其余采用整数旋转，避免Cortex-M3重复计算每个点的三角函数。Q8坐标、Q14旋转系数使中间值保持在32位有符号范围内，原图形的像素量化差异不超过1像素。

表情算法不依赖CMSIS、ESP-IDF或RTOS；时间和麦克风能量由调用者传入。Canvas负责绘图，HeadDisplay负责存储和传输。所有越界像素经过裁剪，避免原圆角实现直接访问越界显存。

当前任务少，采用1ms节拍、协作主循环和外设中断即可：

- 舵机脉冲由TIM2硬件生成，刷屏不会改变脉宽。
- 动作状态机每20ms推进，刷屏每行之间处理通信和控制。
- 屏幕最短调度周期50ms，静止或小范围动画可接近20FPS；复杂动画受绘制与变化面积影响，实际以性能报告为准。每行传输之间持续处理心跳与控制。
- SPI有超时，串口缓冲有上界，独立看门狗防止死循环；链接脚本额外保留4KB栈，不使用动态堆。

## 舵机限位与安全状态

单一配置源：`../shared/ui/head_controller.hpp` 中的 `HeadLimits`。

| 项目 | 默认值 |
| --- | --- |
| 水平基准 | 29° |
| 俯仰基准 | 0° |
| 水平范围 | 9~52° |
| 俯仰范围 | 0~25° |
| 每20ms最大变化 | 1.5°，同时应用原0.18低通平滑 |
| 脉宽换算 | 0°→500us，90°→1500us，180°→2500us |
| PWM周期 | 20000us，50Hz |
| 失联阈值 | 1000ms没有有效STATE |

这些范围取自旧固件使用过的动作，仍需与新装配机械校准。上电不输出PWM；有效STATE必须显式带SERVO_ENABLE才开启。失联后关闭两个通道，停止输出脉冲，但这不是切断舵机电源，也不保证机构仍有保持力。ESP32远程模式的GimbalService启动后会请求使能。

## 协议版本1

两段串口均为115200、8N1、无硬件流控；ESP的唯一发送任务每100ms发送一次STATE，避免多个业务任务交叉写串口。

```text
偏移  0   1     2       3        4         5        6...        最后2字节
     A5  5A  version   type   sequence   length   payload      CRC16_LE
```

最大payload=64，最大帧=72。多字节字段小端。CRC-16/CCITT-FALSE：初值FFFF、多项式1021、不反射、无最终异或；覆盖version至payload，不含帧头和CRC。标准检查向量`123456789`→29B1。

不要直接发送C/C++结构体：对齐、填充和本机字节序不是协议。

| type | 方向 | payload | 行为 |
| --- | --- | --- | --- |
| 01 PING | ESP→C8 | 空 | 立即返回STATUS，sequence回显 |
| 10 STATE | ESP→C8 | 10字节 | 原子更新期望状态，返回ACK |
| 11 CAMERA | ESP→C8 | 1字节 | 0休眠，1异步唤醒，返回ACK |
| 80 STATUS | C8→ESP | 36字节 | 每200ms发送，也响应PING |
| 81 ACK | C8→ESP | 2字节 | 原type、结果码，sequence回显 |

ACK结果码：0成功、1长度错误、2字段值错误、3不支持类型。坏CRC直接丢弃，不回复，避免对噪声产生应答风暴。帧内字节间隔大于100ms时丢弃半帧，错误后逐字节查找下一帧头。

### STATE payload

| 偏移 | 大小 | 字段 |
| --- | --- | --- |
| 0 | 1 | emotion，0~11，与表情预览顺序一致 |
| 1 | 1 | energy，0~255 |
| 2 | 1 | gesture：0无手势、1点头、2摇头、3回正 |
| 3 | 1 | flags：bit0使能舵机，其他位必须为0 |
| 4 | 2 | pan，0.01°单位 |
| 6 | 2 | tilt，0.01°单位 |
| 8 | 2 | event_id，新的目标角度或新手势必须递增 |

心跳重发时保持event_id不变。C8只对新的event_id改变动作目标或启动手势，避免一次点头因重发变成不停点头。表情、音量和使能位不依赖event_id。角度大于180°的包拒绝，合法范围内再按机械限位裁剪。

ESP收到C8报告相同event_id后，会清除待重发的手势，但不改事件编号，因此不会打断C8正在执行的动作，也能避免C8重启后重播过期手势。CRC不提供身份认证；本协议用于板内有线链路。

### STATUS payload

| 偏移 | 大小 | 字段 |
| --- | --- | --- |
| 0/4/8/12/16 | 各4 | uptime_ms、帧数、CRC正确帧数、CRC错误数、UART错误数 |
| 20/22 | 各2 | 平滑输出pan/tilt，0.01° |
| 24/26 | 各2 | 当前控制目标pan/tilt，0.01° |
| 28 | 2 | 已接受event_id |
| 30 | 1 | 屏幕当前emotion |
| 31 | 1 | bit0 PWM开启、bit1在线、bit2摄像头就绪、bit3使用HSE |
| 32 | 1 | 最近接受STATE的sequence |
| 33 | 1 | 保留0 |
| 34 | 2 | 固件版本，当前0100（1.0） |

摄像头唤醒：PWDN拉低、RESET保持低10ms，再将RESET拉高，等待20ms后设置READY。ACK代表命令已接受，READY才代表时序完成；不代表OV5640已经被ESP识别或采集成功。重复唤醒命令不会复位正在工作的相机。摄像头服务启动时先请求休眠再显式调用`requestCamera(true)`，等待READY后才访问SCCB。

可手工核对的PING帧（sequence=7）：`A5 5A 01 01 07 00 D3 5C`。

## 编译、测试、烧录

在仓库根目录执行：

```bash
./scripts/test_head.sh        # 本机clang++、ASan/UBSan、Python PTY测试
./scripts/build_head.sh       # 现有stm32-builder Docker镜像
./scripts/flash_head.sh       # 再核实目标，备份原固件，写入并由st-flash校验
./scripts/build_brain.sh      # 现有esp32-builder Docker镜像
```

`build_head.sh`把整个仓库挂载到`/workspace`；`build_brain.sh`额外挂载公共源码和协议目录。供应商依赖已按固定版本放入vendor，不需要构建时联网。

输出：`build/firmware_head.elf`、`.bin`、`.hex`、`.map`。Flash限制64KB，RAM20KB，并通过链接断言保留4KB栈。ESP默认`CONFIG_ROBOT_HEAD_REMOTE=y`，不会初始化占用摄像头GPIO的旧SPI/LEDC驱动。禁止在新接线下关闭该选项。

### Luckfox部署

2026-10-18已完成Mini B首次刷机和部署。USB运行设备为`2207:0019`，本次ADB序列号为`720ec8b012a74867`；以实际`adb devices -l`结果为准。

首次刷机须先确认Mini A/B版本和启动介质：[官方刷机说明](https://wiki.luckfox.com/zh/Luckfox-Pico-Plus-Mini/Flash-image/)。Mini A没有板载SPI NAND，系统必须放MicroSD卡；Mini B可选对应板型的Flash或MicroSD镜像。镜像名称必须匹配板型及介质，不能把Flash更新包直接当作整卡原始镜像写入。

采用用户从官方资料网盘下载的`Luckfox_Pico_Mini_Flash_250607.zip`。压缩包CRC检查通过，boot中的板型为Luckfox Pico Mini，分区为SPI NAND，工具读取Flash为127MB；这些检查用于确认完整性与板型，记录的SHA256用于复现，不是厂商签名认证。实际写入的`update.img` SHA256为`2ae7597d0fd203493b9cb42c7dacf24668f76446b21abbe795be129af3b388bf`。

工具位于`build/luckfox_setup/upgrade_tool_v2.44_for_mac/`，来源为上述官方页的macOS下载链接。macOS v2.44在普通管道输出下发生过崩溃，改用PTY交互终端；首次下载RAM引导程序后等待重新枚举超时，设备仍可测试和读取Flash，第二次`uf update.img`成功，最终报告`Download Firmware Success`和`Upgrade firmware ok.`。记录保存在`artifacts/luckfox_setup_20261018.json`。未操作任何MicroSD卡。

系统启动后先确认`python3 --version`可用（桥依赖Python 3标准库；若镜像未包含，需在SDK的Buildroot配置中加入Python 3并重新构建），再部署下面的程序。

`/etc/luckfox.cfg`中的`UART3_M1_STATUS=1`和`UART4_M1_STATUS=1`已由官方`luckfox-config load`应用；启动时会自动加载。原配置备份为`/etc/luckfox.cfg.before-head-bridge`。没有getty占用这两路UART。

部署目录为`/userdata/head_bridge/`，包含`head_protocol.py`、`head_bridge.py`、`head_supervisor.py`及测试脚本。生产文件已与电脑源码核对SHA256。正式服务由`/etc/init.d/S99zheadbridge`启动，排在官方串口配置加载之后：

```bash
/etc/init.d/S99zheadbridge status
/etc/init.d/S99zheadbridge stop
/etc/init.d/S99zheadbridge start
/etc/init.d/S99zheadbridge restart
```

`head_supervisor.py`使用内核flock确保单实例；桥异常退出后等待3秒重启，收到TERM则停止并回收子进程。PID/锁放`/run/head-bridge/`，日志发送syslog，可用`tail /var/log/messages`查看；不持续写Flash。服务不自己产生STATE心跳，ESP离线时C8仍会超时关闭PWM。

**调试时先停止服务**，再手动启动，避免两个进程抢读UART：

```bash
python3 head_bridge.py --esp /dev/ttyS3 --head /dev/ttyS4 --baud 115200
```

桥只允许ESP→C8的PING/STATE/CAMERA，及C8→ESP的STATUS/ACK。发送队列限制4096字节；消费者失效时退出而非无限积压，C8最终由心跳超时停PWM。不同时启动两份桥。

实机测试命令（Luckfox上执行）：

```bash
/etc/init.d/S99zheadbridge stop
cd /userdata/head_bridge
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v test_head_bridge.ProtocolTest.test_golden test_head_bridge.ProtocolTest.test_real_bridge
PYTHONDONTWRITEBYTECODE=1 python3 probe_head.py
PYTHONDONTWRITEBYTECODE=1 python3 probe_head.py --through-bridge
/etc/init.d/S99zheadbridge start
```

实机约有33MB Linux可用总内存，服务运行后MemAvailable约7.8MB。把完整unittest、看护、桥、第二份看护同时放板上测试曾触发OOM，因此服务生命周期测试改由电脑上的`tools/test_luckfox_service.py`通过ADB驱动；完整Python三项测试在电脑通过。板上上述两项PTY测试通过。不要在板上同时运行整套看护单元测试与正式服务。UART驱动日志提示未配置DMA并改用中断，这是本次115200波特率下实际使用的模式，实体收发测试已通过。

ESP网页遥测增加`head_online`和`head_age_ms`。源码修正版的`head_online`要求回传新鲜且C8的LINK_ONLINE状态有效；`head_age_ms`只表示回传年龄，可用来区分单向回传与双向控制。离线时角度是最后一次报告值，不能当作实时实测位置。

三板测试由电脑上的`tools/test_head_end_to_end.py`及Node.js 22自带WebSocket驱动。它用ESP原有网页协议发命令，通过运行中C8的SWD内存只读快照核对表情（不注入SWD命令、不暂停CPU，避免暂停UART中断制造假错误），再检查ESP返回的计算角度；舵机动力电源须断开，不能把该测试当成机械位置验收。运行需先启动下述OpenOCD，但把最后的`-c 'reset run'`去掉以免重置正在工作的C8。提供`--adb`和`--serial`时，还会停止正式桥，运行`status_only_probe.py`五秒模拟单向控制断线，随后恢复正式服务。

本次命令（替换IP和ADB序列号为现场实际值）：

```bash
python3 tools/test_head_end_to_end.py --ip 192.168.1.169 \
  --adb /Users/mjt/Desktop/workplace/drone/firmware_head/build/luckfox_setup/platform-tools/adb \
  --serial 720ec8b012a74867 --output firmware_head/artifacts/head_end_to_end_test.json
```

当前ESP网页控制台：`http://192.168.1.169/`；重新联网后IP可能变化，以串口日志为准。

### SWD板上测试

先停止占用ST-Link的其他程序。在一个终端启动：

```bash
openocd -c 'bindto 127.0.0.1' -c 'gdb_port disabled' -c 'telnet_port disabled' \
  -c 'tcl_port 6666' -f interface/stlink.cfg -c 'transport select hla_swd' \
  -f target/stm32f1x.cfg -c 'adapter speed 1000' -c init -c 'reset run'
```

另一个终端：

```bash
python3 tools/head_swd_test.py          # 默认不使能舵机
python3 tools/head_swd_test.py --motion # 基准附近1°；确认机构可动且供电正确后再用
```

测试脚本读取本次ELF的符号地址，不把RAM地址硬编码进主机工具。`g_head_diag`可供观察状态，`g_swd_frame/g_swd_length`是调试注入通道。UART与SWD计数分开，不把模拟注入冒充真实UART收包。

`g_lcd_transfer_bytes/g_lcd_changed_tiles`记录最近一帧发送字节数及变化块数。运行`python3 tools/benchmark_head_display.py --ip <ESP地址> --output firmware_head/artifacts/display_performance.json`，可通过现有网页接口下发8种表情，读取实板帧计数测FPS；单帧耗时、字节数是50ms周期采样的近似分布。此接口没有BOOTING/THINKING/WINK/COOL命令，不能把它们记成此次实板测速已覆盖。工具只读SWD、不暂停CPU，结束恢复NORMAL、29°/0°并关闭自主动作；测速时保持舵机动力电源断开。

若UART错误数持续上升且PA10为低电平，检查Luckfox是否上电、UART4是否启用、11脚TX是否确实连到C8 A10。不要用SWD测试通过来忽略这一接线/配置问题。

恢复原固件前先核实目标仍是C8，停止OpenOCD，再执行：

```bash
st-flash --reset write firmware_head/artifacts/c8_before_migration_20261018.bin 0x08000000
```

备份和构建二进制被仓库原有`*.bin/*.elf`规则忽略，但保留在当前工作目录；需要长期归档时另行保存。
