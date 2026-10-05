#pragma once

// 自动内嵌的前端 SPA 静态网页资源 (存储在 Flash 中，零外部文件系统依赖)
static const char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
  <title>CYBER-ROBOT // 双芯片座舱系统</title>
  <style>
    :root {
      --bg: #090d16;
      --surface: #101726;
      --surface-card: #151f32;
      --surface-card-hover: #1a273f;
      --border: rgba(0, 240, 255, 0.2);
      --border-subtle: rgba(255, 255, 255, 0.07);
      --cyan: #00f0ff;
      --cyan-glow: rgba(0, 240, 255, 0.3);
      --green: #10b981;
      --red: #ef4444;
      --yellow: #f59e0b;
      --purple: #a855f7;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --text-dim: #64748b;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", sans-serif;
      -webkit-tap-highlight-color: transparent;
      user-select: none;
    }

    body {
      background: var(--bg);
      background-image: 
        radial-gradient(ellipse at 50% 0%, rgba(0, 240, 255, 0.08) 0%, transparent 65%),
        radial-gradient(ellipse at 50% 100%, rgba(168, 85, 247, 0.06) 0%, transparent 65%);
      color: var(--text);
      height: 100vh;
      height: 100dvh;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    /* ========================================================
       1. 顶部全局固定状态栏 (APP 顶栏)
       ======================================================== */
    header {
      flex-shrink: 0;
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 12px;
      padding-top: max(8px, env(safe-area-inset-top));
      padding-left: max(12px, env(safe-area-inset-left));
      padding-right: max(12px, env(safe-area-inset-right));
      background: rgba(16, 23, 38, 0.95);
      border-bottom: 1px solid var(--border-subtle);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      z-index: 100;
    }

    .brand {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .brand-logo {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      background: var(--cyan);
      box-shadow: 0 0 10px var(--cyan);
      animation: pulse 2s infinite ease-in-out;
    }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.35; transform: scale(0.85); }
    }

    .brand-title {
      font-size: 13px;
      font-weight: 800;
      letter-spacing: 0.8px;
      color: #fff;
    }

    .status-capsules {
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .capsule {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      font-size: 11px;
      padding: 3px 7px;
      border-radius: 12px;
      font-weight: 600;
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--border-subtle);
      white-space: nowrap;
    }

    .capsule-dot {
      width: 6px;
      height: 6px;
      border-radius: 50%;
      background: currentColor;
    }

    .cap-online { color: var(--green); border-color: rgba(16, 185, 129, 0.3); background: rgba(16, 185, 129, 0.12); }
    .cap-offline { color: var(--red); border-color: rgba(239, 68, 68, 0.3); background: rgba(239, 68, 68, 0.12); }
    .cap-battery { color: var(--cyan); border-color: rgba(0, 240, 255, 0.3); background: rgba(0, 240, 255, 0.12); }
    .cap-alarm { color: var(--red); border-color: var(--red); background: rgba(239, 68, 68, 0.25); animation: pulse 1s infinite; }

    /* ========================================================
       2. 中间多标签内容区域 (可自适应滚动)
       ======================================================== */
    main {
      flex: 1;
      overflow-y: auto;
      overflow-x: hidden;
      padding: 10px 12px;
      padding-left: max(12px, env(safe-area-inset-left));
      padding-right: max(12px, env(safe-area-inset-right));
      display: flex;
      flex-direction: column;
      gap: 10px;
      -webkit-overflow-scrolling: touch;
    }

    .tab-content {
      display: none;
      flex-direction: column;
      gap: 10px;
      width: 100%;
      max-width: 1100px;
      margin: 0 auto;
    }

    .tab-content.active {
      display: flex;
    }

    /* 模块通用卡片容器 */
    .panel {
      background: var(--surface-card);
      border: 1px solid var(--border-subtle);
      border-radius: 12px;
      padding: 12px;
      display: flex;
      flex-direction: column;
      gap: 10px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
    }

    .panel-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      padding-bottom: 6px;
    }

    .panel-title {
      font-size: 13px;
      font-weight: 700;
      color: var(--cyan);
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .panel-subtitle {
      font-size: 11px;
      color: var(--text-dim);
      font-weight: 400;
    }

    /* ========================================================
       TAB 1: 驾驶座舱 (专业移动端车载 HUD 与双区触控)
       ======================================================== */
    /* HUD 抬头微缩显示条 */
    .cockpit-hud {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 6px;
      background: rgba(0, 0, 0, 0.4);
      padding: 8px;
      border-radius: 10px;
      border: 1px solid var(--border);
    }

    .hud-box {
      display: flex;
      flex-direction: column;
      align-items: center;
      text-align: center;
      gap: 2px;
    }

    .hud-lbl {
      font-size: 10px;
      color: var(--text-muted);
    }

    .hud-val {
      font-size: 13px;
      font-family: monospace;
      font-weight: 700;
      color: #fff;
    }

    /* 姿态地平仪微缩条 */
    .mini-horizon-wrapper {
      position: relative;
      width: 100%;
      height: 36px;
      background: rgba(10, 16, 26, 0.85);
      border-radius: 6px;
      overflow: hidden;
      display: flex;
      align-items: center;
      justify-content: center;
      border: 1px solid rgba(255, 255, 255, 0.06);
      margin-top: 2px;
    }

    .mini-horizon-center {
      position: absolute;
      width: 16px;
      height: 16px;
      border: 1.5px solid rgba(255, 255, 255, 0.6);
      border-radius: 50%;
      pointer-events: none;
      z-index: 2;
    }
    .mini-horizon-center::before {
      content: "";
      position: absolute;
      top: 50%; left: -8px; right: -8px; height: 1.5px;
      background: rgba(255, 255, 255, 0.6);
      transform: translateY(-50%);
    }

    .mini-horizon-bar {
      position: absolute;
      width: 80%;
      height: 3px;
      background: linear-gradient(90deg, transparent, var(--cyan), transparent);
      box-shadow: 0 0 8px var(--cyan);
      transition: transform 0.1s linear;
      z-index: 1;
    }

    /* 触控操控双区域 (手机横向左右并列或竖向并列) */
    .drive-control-stage {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      align-items: center;
      margin-top: 4px;
    }

    @media (max-width: 480px) {
      .drive-control-stage {
        grid-template-columns: 1fr;
      }
    }

    .control-subpanel {
      background: rgba(0, 0, 0, 0.35);
      border: 1px solid rgba(255, 255, 255, 0.05);
      border-radius: 12px;
      padding: 10px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 8px;
    }

    .subpanel-tag {
      font-size: 11px;
      font-weight: 700;
      color: var(--cyan);
      display: flex;
      justify-content: space-between;
      width: 100%;
    }

    /* 手机虚拟比例摇杆 */
    .joystick-base {
      width: 150px;
      height: 150px;
      border-radius: 50%;
      background: radial-gradient(circle, rgba(0, 240, 255, 0.05) 0%, rgba(16, 24, 40, 0.8) 70%);
      border: 2px solid rgba(0, 240, 255, 0.3);
      box-shadow: inset 0 0 16px rgba(0, 0, 0, 0.6);
      position: relative;
      touch-action: none;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .joystick-thumb {
      width: 54px;
      height: 54px;
      border-radius: 50%;
      background: radial-gradient(circle at 35% 35%, #00f0ff, #0284c7);
      box-shadow: 0 4px 12px rgba(0, 240, 255, 0.5);
      position: absolute;
      top: calc(50% - 27px);
      left: calc(50% - 27px);
      cursor: grab;
      touch-action: none;
    }

    /* 云台快捷控制与雷达微视 */
    .gimbal-pad-box {
      width: 100%;
      display: flex;
      flex-direction: column;
      gap: 8px;
    }

    .gimbal-radar-strip {
      display: flex;
      align-items: center;
      gap: 10px;
      background: rgba(0, 0, 0, 0.4);
      padding: 6px 8px;
      border-radius: 8px;
    }

    .gimbal-radar-circle {
      width: 48px;
      height: 48px;
      border-radius: 50%;
      border: 1px solid rgba(168, 85, 247, 0.5);
      background: radial-gradient(circle, rgba(168, 85, 247, 0.1) 0%, rgba(0,0,0,0.6) 80%);
      position: relative;
      flex-shrink: 0;
    }
    .gimbal-radar-circle::before {
      content: ""; position: absolute; top: 50%; left: 0; right: 0; height: 1px; background: rgba(168, 85, 247, 0.25);
    }
    .gimbal-radar-circle::after {
      content: ""; position: absolute; left: 50%; top: 0; bottom: 0; width: 1px; background: rgba(168, 85, 247, 0.25);
    }

    .gimbal-dot {
      position: absolute;
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background: var(--purple);
      box-shadow: 0 0 8px var(--purple);
      transform: translate(-50%, -50%);
      top: 50%;
      left: 50%;
      transition: top 0.08s ease-out, left 0.08s ease-out;
    }

    .gimbal-dpad-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 6px;
      width: 100%;
    }

    .dpad-btn {
      background: rgba(168, 85, 247, 0.08);
      border: 1px solid rgba(168, 85, 247, 0.25);
      color: #fff;
      padding: 8px 4px;
      border-radius: 8px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 2px;
    }
    .dpad-btn:active {
      background: var(--purple);
      color: #fff;
    }

    .btn-stop-huge {
      background: rgba(239, 68, 68, 0.18);
      border: 1px solid var(--red);
      color: var(--red);
      font-weight: 800;
      padding: 10px;
      border-radius: 8px;
      font-size: 14px;
      cursor: pointer;
      width: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
    }
    .btn-stop-huge:active {
      background: var(--red);
      color: #fff;
      box-shadow: 0 0 16px var(--red);
    }

    /* ========================================================
       TAB 2: 双芯遥测 (硬件档案、存储硬盘、内存与动力学)
       ======================================================== */
    .metric-grid-2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px; }
    .metric-grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; }
    .metric-grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }

    @media (max-width: 560px) {
      .metric-grid-4 { grid-template-columns: repeat(2, 1fr); }
      .metric-grid-3 { grid-template-columns: repeat(2, 1fr); }
    }

    .metric-chip {
      background: rgba(255, 255, 255, 0.03);
      padding: 6px 8px;
      border-radius: 6px;
      display: flex;
      flex-direction: column;
      gap: 2px;
      border: 1px solid rgba(255, 255, 255, 0.04);
    }

    .metric-chip-title {
      font-size: 10px;
      color: var(--text-muted);
    }

    .metric-chip-value {
      font-size: 12px;
      font-family: monospace;
      font-weight: 700;
      color: var(--cyan);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .progress-bar-bg {
      width: 100%;
      height: 4px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 2px;
      overflow: hidden;
      margin-top: 3px;
    }

    .progress-bar-fill {
      height: 100%;
      background: linear-gradient(90deg, var(--cyan), var(--purple));
      width: 0%;
      transition: width 0.25s ease;
    }

    /* ========================================================
       TAB 3: 拟人交互 & TAB 4: AI 智脑
       ======================================================== */
    .btn-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 6px;
    }
    @media (max-width: 480px) {
      .btn-grid { grid-template-columns: repeat(2, 1fr); }
    }

    .action-btn {
      padding: 9px 8px;
      border: 1px solid rgba(0, 240, 255, 0.25);
      background: rgba(0, 240, 255, 0.05);
      color: #fff;
      border-radius: 8px;
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 4px;
    }
    .action-btn:active {
      transform: scale(0.96);
      background: var(--cyan);
      color: #000;
    }

    .action-btn.purple {
      border-color: rgba(168, 85, 247, 0.3);
      background: rgba(168, 85, 247, 0.08);
    }
    .action-btn.purple:active {
      background: var(--purple);
      color: #fff;
    }

    .slider-row {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 12px;
    }
    .slider-label { width: 68px; color: var(--text-muted); }
    .slider-val { width: 44px; font-family: monospace; color: var(--cyan); font-weight: 700; text-align: right; }
    .slider-input { flex: 1; accent-color: var(--cyan); cursor: pointer; }

    /* 聊天视窗 */
    .chat-log {
      height: 180px;
      overflow-y: auto;
      background: rgba(0, 0, 0, 0.4);
      border-radius: 8px;
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 6px;
      font-size: 12px;
      border: 1px solid var(--border-subtle);
    }
    .chat-msg {
      padding: 6px 10px;
      border-radius: 8px;
      max-width: 85%;
      line-height: 1.4;
    }
    .chat-msg.user { align-self: flex-end; background: rgba(168, 85, 247, 0.25); color: #f1f5f9; }
    .chat-msg.robot { align-self: flex-start; background: rgba(0, 240, 255, 0.15); color: #38bdf8; }

    .chat-input-row { display: flex; gap: 6px; }
    .chat-input-row input {
      flex: 1; background: rgba(0, 0, 0, 0.4); border: 1px solid rgba(255, 255, 255, 0.1);
      color: #fff; padding: 7px 10px; border-radius: 8px; outline: none; font-size: 12px;
    }
    .chat-input-row button {
      background: var(--purple); color: #fff; border: none; padding: 0 14px; border-radius: 8px; font-weight: 600; cursor: pointer;
    }

    /* 拟人滑动开关 */
    .switch { position: relative; display: inline-block; width: 38px; height: 20px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .toggle-slider {
      position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
      background-color: rgba(255, 255, 255, 0.15); transition: .25s; border-radius: 20px;
    }
    .toggle-slider:before {
      position: absolute; content: ""; height: 14px; width: 14px; left: 3px; bottom: 3px;
      background-color: #fff; transition: .25s; border-radius: 50%;
    }
    input:checked + .toggle-slider { background-color: var(--cyan); }
    input:checked + .toggle-slider:before { transform: translateX(18px); background-color: #000; }

    /* ========================================================
       3. 底部专属 App 导航分栏 (Bottom Tab Bar)
       ======================================================== */
    nav.bottom-nav {
      flex-shrink: 0;
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      background: rgba(16, 23, 38, 0.98);
      border-top: 1px solid var(--border-subtle);
      padding-bottom: max(6px, env(safe-area-inset-bottom));
      backdrop-filter: blur(20px);
      -webkit-backdrop-filter: blur(20px);
      z-index: 100;
    }

    .nav-item {
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 13px 0;
      color: var(--text-dim);
      font-size: 13px;
      font-weight: 600;
      letter-spacing: 0.5px;
      cursor: pointer;
      transition: all 0.15s ease;
      border-top: 2px solid transparent;
    }

    .nav-item.active {
      color: var(--cyan);
      border-top: 2px solid var(--cyan);
      background: rgba(0, 240, 255, 0.05);
    }
  </style>
</head>
<body>

  <!-- =========================
       1. 顶部全局极简状态栏
       ========================= -->
  <header>
    <div class="brand">
      <div class="brand-logo"></div>
      <div class="brand-title">CYBER-ROBOT</div>
    </div>
    <div class="status-capsules">
      <div class="capsule cap-battery" id="capBattery">
        <span id="txtBat">12.1V</span>
      </div>
      <div class="capsule cap-offline" id="capChassis">
        <div class="capsule-dot"></div><span>STM32</span>
      </div>
      <div class="capsule cap-offline" id="capBrain">
        <div class="capsule-dot"></div><span>ESP32</span>
      </div>
      <div class="capsule cap-alarm" id="capAlarm" style="display: none;">
        <span>跌倒倾覆!</span>
      </div>
    </div>
  </header>

  <!-- =========================
       2. 中间多标签内容区域
       ========================= -->
  <main>

    <!-- ────────────────────────────────────────────────────────
         TAB 1: 驾驶座舱 (Drive Cockpit)
         ──────────────────────────────────────────────────────── -->
    <div class="tab-content active" id="view-cockpit">
      <!-- 抬头 HUD 仪表 (速度、俯仰姿态、电池、云台朝向) -->
      <div class="panel">
        <div class="cockpit-hud">
          <div class="hud-box">
            <span class="hud-lbl">线速度</span>
            <span class="hud-val" id="hudSpeed" style="color:var(--cyan);">0 <small style="font-size:10px;">mm/s</small></span>
          </div>
          <div class="hud-box">
            <span class="hud-lbl">俯仰角</span>
            <span class="hud-val" id="hudPitch">0.0°</span>
          </div>
          <div class="hud-box">
            <span class="hud-lbl">动力电量</span>
            <span class="hud-val" id="hudBatPct" style="color:var(--green);">90%</span>
          </div>
          <div class="hud-box">
            <span class="hud-lbl">头部朝向</span>
            <span class="hud-val" id="hudGimbal">29° / 0°</span>
          </div>
        </div>

        <!-- 拟真机体俯仰微缩地平条 -->
        <div class="mini-horizon-wrapper">
          <div class="mini-horizon-center"></div>
          <div class="mini-horizon-bar" id="miniHorizonBar"></div>
          <span style="position:absolute; right:6px; bottom:2px; font-size:9px; color:var(--text-dim); font-family:monospace;">PITCH HORIZON</span>
        </div>
      </div>

      <!-- 双区触控操控台 (左手摇杆 + 右手云台与急停) -->
      <div class="drive-control-stage">
        <!-- 左区: 底盘虚拟比例摇杆 (完全独立控制底盘) -->
        <div class="control-subpanel">
          <div class="subpanel-tag">
            <span>底盘比例摇杆</span>
            <span id="joyMetrics" style="font-family:monospace; color:var(--cyan);">0 mm/s | 0 mrad/s</span>
          </div>
          <div class="joystick-base" id="joystickBase">
            <div class="joystick-thumb" id="joystickThumb"></div>
          </div>
          <span style="font-size:10px; color:var(--text-dim);">拖拽无级差速控车，松手刹车急停</span>
        </div>

        <!-- 右区: 二自由度头部云台微调与急停 -->
        <div class="control-subpanel">
          <div class="subpanel-tag">
            <span>云台姿态与急停</span>
            <span id="gimbalCurText" style="font-family:monospace; color:var(--purple);">29.0° / 0.0°</span>
          </div>
          <div class="gimbal-pad-box">
            <div class="gimbal-radar-strip">
              <div class="gimbal-radar-circle">
                <div class="gimbal-dot" id="gimbalRadarDot"></div>
              </div>
              <div style="flex:1; display:flex; flex-direction:column; gap:2px;">
                <span style="font-size:10px; color:var(--text-muted);">水平 Pan: <b id="gimbalPanVal" style="color:var(--cyan);">29.0°</b></span>
                <span style="font-size:10px; color:var(--text-muted);">垂直 Tilt: <b id="gimbalTiltVal" style="color:var(--purple);">0.0°</b></span>
              </div>
            </div>

            <!-- 云台快速步进微调按钮 -->
            <div class="gimbal-dpad-grid">
              <button class="dpad-btn" onclick="adjustGimbal(0, 10)"><span>抬头</span></button>
              <button class="dpad-btn" onclick="sendGimbalGesture('RESET')"><span>居中</span></button>
              <button class="dpad-btn" onclick="adjustGimbal(0, -10)"><span>低头</span></button>
              <button class="dpad-btn" onclick="adjustGimbal(-15, 0)"><span>左转</span></button>
              <button class="dpad-btn" onclick="sendGimbalGesture('NOD')"><span>点头</span></button>
              <button class="dpad-btn" onclick="adjustGimbal(15, 0)"><span>右转</span></button>
            </div>

            <!-- 全局大尺寸急停按钮 -->
            <button class="btn-stop-huge" onclick="sendCmd('STOP')">
              急停刹车 (EMERGENCY STOP)
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- ────────────────────────────────────────────────────────
         TAB 2: 双芯遥测 (Dual-Chip Telemetry & Storage)
         ──────────────────────────────────────────────────────── -->
    <div class="tab-content" id="view-telemetry">
      <!-- 芯片 A: STM32G473 底盘全景 -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <span>STM32G473 底盘主控</span>
            <span class="panel-subtitle">(ARM Cortex-M4F @ 160MHz)</span>
          </div>
          <span class="capsule cap-online" id="stmStatusPill" style="font-size:10px;">在线 460.8k</span>
        </div>

        <!-- 芯片档案与存储空间 (内存 / 硬盘 / 96-Bit UID) -->
        <div style="background:rgba(0,0,0,0.3); padding:8px; border-radius:8px; display:flex; flex-direction:column; gap:6px;">
          <div style="display:flex; justify-content:space-between; font-size:11px; flex-wrap:wrap; gap:4px;">
            <span style="color:var(--text-muted);">96-Bit 硬件唯一序列号 (UID):</span>
            <span style="font-family:monospace; color:var(--cyan); font-weight:700;" id="valStmUid">--</span>
          </div>

          <div class="metric-grid-2">
            <div style="display:flex; flex-direction:column; gap:2px;">
              <div style="display:flex; justify-content:space-between; font-size:11px;">
                <span style="color:var(--text-muted);">片上 Flash ("硬盘"):</span>
                <span style="font-family:monospace; color:#fff;" id="valStmFlash">32K / 256K (12%)</span>
              </div>
              <div class="progress-bar-bg"><div class="progress-bar-fill" id="barStmFlash" style="width:12%;"></div></div>
            </div>

            <div style="display:flex; flex-direction:column; gap:2px;">
              <div style="display:flex; justify-content:space-between; font-size:11px;">
                <span style="color:var(--text-muted);">片上 SRAM ("内存"):</span>
                <span style="font-family:monospace; color:#fff;" id="valStmSram">堆余 60K / 总 112K</span>
              </div>
              <div class="progress-bar-bg"><div class="progress-bar-fill" id="barStmSram" style="background:linear-gradient(90deg, var(--green), var(--cyan)); width:46%;"></div></div>
            </div>
          </div>
        </div>

        <!-- 六轴 IMU ICM-42605 姿态解算 -->
        <div class="metric-grid-4">
          <div class="metric-chip">
            <span class="metric-chip-title">俯仰角 (Pitch)</span>
            <span class="metric-chip-value" id="valPitch">0.0°</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">横滚角 (Roll)</span>
            <span class="metric-chip-value" id="valRoll">0.0°</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">角速度 (Rate)</span>
            <span class="metric-chip-value" id="valPitchRate">0.0 °/s</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">加速度俯仰 (Acc)</span>
            <span class="metric-chip-value" id="valAccPitch">0.0°</span>
          </div>
        </div>

        <!-- 双轮差速动力学与电机输出 -->
        <div class="metric-grid-4">
          <div class="metric-chip">
            <span class="metric-chip-title">综合测速</span>
            <span class="metric-chip-value" id="valSpeed">0 mm/s</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">左轮速度/脉冲</span>
            <span class="metric-chip-value" id="valLeftWheel">0 / 0</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">右轮速度/脉冲</span>
            <span class="metric-chip-value" id="valRightWheel">0 / 0</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">电机驱动 PWM</span>
            <span class="metric-chip-value" id="valMotorPwm">L:0 / R:0</span>
          </div>
        </div>

        <!-- 动力电池与通信链路 -->
        <div class="metric-grid-3">
          <div class="metric-chip">
            <span class="metric-chip-title">动力电池电压</span>
            <span class="metric-chip-value" id="valBattery">12.10 V (90%)</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">底盘健康标志</span>
            <span class="metric-chip-value" id="valChassisStatus" style="color:var(--green);">IMU就绪 | 待机</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">跨芯片收发统计</span>
            <span class="metric-chip-value" id="valChassisComm">0 KB / 0 帧</span>
          </div>
        </div>
      </div>

      <!-- 芯片 B: ESP32-S3 大脑中枢全景 -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <span>ESP32-S3 大脑中枢</span>
            <span class="panel-subtitle">(Xtensa Dual-Core @ 240MHz)</span>
          </div>
          <span class="capsule cap-online" id="espStatusPill" style="font-size:10px;">AP 192.168.4.1</span>
        </div>

        <div class="metric-grid-2">
          <div class="metric-chip">
            <span class="metric-chip-title">系统运行时长</span>
            <span class="metric-chip-value" id="valUptime">00:00:00</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">外部 SPI Flash ("硬盘")</span>
            <span class="metric-chip-value">16 MB (固件975K)</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">内部 SRAM (Free/Min)</span>
            <span class="metric-chip-value" id="valSram">-- / -- KB</span>
          </div>
          <div class="metric-chip">
            <span class="metric-chip-title">8MB PSRAM (Free / Total)</span>
            <span class="metric-chip-value" id="valPsram">-- / 8192 KB</span>
          </div>
        </div>

        <!-- PSRAM 占用率 -->
        <div style="display:flex; flex-direction:column; gap:2px;">
          <div style="display:flex; justify-content:space-between; font-size:11px; color:var(--text-muted);">
            <span>PSRAM 内存健康占用率</span>
            <span id="valPsramPct" style="font-family:monospace; color:#fff;">0%</span>
          </div>
          <div class="progress-bar-bg"><div class="progress-bar-fill" id="barPsram"></div></div>
        </div>
      </div>
    </div>

    <!-- ────────────────────────────────────────────────────────
         TAB 3: 拟人交互 (Emotions & Audio)
         ──────────────────────────────────────────────────────── -->
    <div class="tab-content" id="view-emotions">
      <!-- 拟人表情 -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">赛博拟人表情中枢 (Face Engine)</div>
          <div style="display:flex; align-items:center; gap:6px;">
            <span style="font-size:11px; color:var(--text-muted);">自主拟人模式</span>
            <label class="switch">
              <input type="checkbox" id="autoModeToggle" checked onchange="toggleAutoMode(this.checked)">
              <span class="toggle-slider"></span>
            </label>
          </div>
        </div>

        <div class="btn-grid">
          <button class="action-btn" onclick="setEmotion('NORMAL')">正常眨眼</button>
          <button class="action-btn" onclick="setEmotion('HAPPY')">开心月牙</button>
          <button class="action-btn" onclick="setEmotion('SURPRISED')">震惊大眼</button>
          <button class="action-btn" onclick="setEmotion('SLEEPY')">困倦打盹</button>
          <button class="action-btn" style="border-color: rgba(244, 114, 182, 0.4); color: #f472b6;" onclick="setEmotion('LOVE')">喜爱爱心</button>
          <button class="action-btn" style="border-color: rgba(239, 68, 68, 0.4); color: var(--red);" onclick="setEmotion('ANGRY')">生气警戒</button>
          <button class="action-btn" onclick="setEmotion('CONFUSED')">疑惑挑眉</button>
          <button class="action-btn" onclick="setEmotion('DIZZY')">眩晕打转</button>
          <button class="action-btn purple" style="grid-column: 1 / -1; font-weight: 700;" onclick="triggerDance()">
            赛博特技跳舞秀 (卡点节拍+变脸)
          </button>
        </div>
      </div>

      <!-- 声学系统 -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">声学系统 (MAX98357A & INMP441)</div>
        </div>
        <div class="btn-grid">
          <button class="action-btn" onclick="sendAudioCmd('CHIME')">开机和弦</button>
          <button class="action-btn" onclick="sendAudioCmd('BEEP')">交互提示</button>
          <button class="action-btn" style="border-color: rgba(239, 68, 68, 0.4); color: var(--red);" onclick="sendAudioCmd('ALERT')">警报音效</button>
        </div>
        <div class="slider-row">
          <span class="slider-label">喇叭音量:</span>
          <input type="range" class="slider-input" id="volumeSlider" min="0" max="100" value="40" oninput="onVolumeChange()">
          <span class="slider-val" id="volumeVal">40%</span>
        </div>
        <div style="display:flex; flex-direction:column; gap:2px;">
          <div style="display:flex; justify-content:space-between; font-size:11px; color:var(--text-muted);">
            <span>麦克风实时能量 (VU 电平)</span>
            <span id="valMicEnergy" style="color:var(--green); font-family:monospace; font-weight:700;">0%</span>
          </div>
          <div class="progress-bar-bg"><div class="progress-bar-fill" id="barMicEnergy" style="background:linear-gradient(90deg, var(--green), var(--cyan)); width:0%;"></div></div>
        </div>
      </div>
    </div>

    <!-- ────────────────────────────────────────────────────────
         TAB 4: AI 智脑 (AI Stream Channel)
         ──────────────────────────────────────────────────────── -->
    <div class="tab-content" id="view-chat">
      <div class="panel" style="flex:1;">
        <div class="panel-header">
          <div class="panel-title">AI 智脑流式通道 (WebSocket 全双工)</div>
        </div>
        <div class="chat-log" id="chatLog">
          <div class="chat-msg robot">你好！我是赛博双芯片智能小车。WebSocket 链路就绪！</div>
        </div>
        <div class="chat-input-row">
          <input type="text" id="aiInput" placeholder="输入对话或指令..." onkeydown="if(event.key==='Enter') sendAiMessage()">
          <button onclick="sendAiMessage()">发送</button>
        </div>
      </div>

      <!-- 独立连接输入框 -->
      <div class="panel" style="padding:8px 12px;">
        <div style="display:flex; align-items:center; gap:8px;">
          <span style="font-size:11px; color:var(--text-muted);">WS:</span>
          <input type="text" id="wsUrlInput" style="flex:1; background:rgba(0,0,0,0.3); border:1px solid rgba(255,255,255,0.1); color:#fff; padding:4px 8px; border-radius:6px; font-family:monospace; font-size:11px;">
          <button onclick="reconnectWebSocket()" style="background:var(--cyan); border:none; padding:4px 10px; border-radius:6px; font-weight:700; cursor:pointer;">连接</button>
        </div>
      </div>
    </div>

  </main>

  <!-- =========================
       3. 底部 App 导航栏
       ========================= -->
  <nav class="bottom-nav">
    <div class="nav-item active" onclick="switchTab('cockpit')">
      <span>驾驶座舱</span>
    </div>
    <div class="nav-item" onclick="switchTab('telemetry')">
      <span>双芯遥测</span>
    </div>
    <div class="nav-item" onclick="switchTab('emotions')">
      <span>拟人交互</span>
    </div>
    <div class="nav-item" onclick="switchTab('chat')">
      <span>AI智脑</span>
    </div>
  </nav>

  <script>
    // ----------------------------------------------------
    // 1. 标签页切换逻辑
    // ----------------------------------------------------
    function switchTab(tabId) {
      document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));

      const targetView = document.getElementById('view-' + tabId);
      if (targetView) targetView.classList.add('active');

      const tabs = ['cockpit', 'telemetry', 'emotions', 'chat'];
      const idx = tabs.indexOf(tabId);
      if (idx !== -1) {
        document.querySelectorAll('.nav-item')[idx].classList.add('active');
      }
    }

    // ----------------------------------------------------
    // 2. WebSocket 全双工流式通信
    // ----------------------------------------------------
    let ws = null;
    const wsUrlInput = document.getElementById('wsUrlInput');
    const defaultHost = window.location.host || '192.168.4.1';
    wsUrlInput.value = `ws://${defaultHost}/ws`;

    function initWebSocket(url) {
      if (ws) ws.close();

      const capBrain = document.getElementById('capBrain');
      capBrain.className = 'capsule cap-offline';
      capBrain.innerHTML = '<div class="capsule-dot"></div><span>连接中</span>';

      try {
        ws = new WebSocket(url);
      } catch (e) {
        capBrain.innerHTML = '<div class="capsule-dot"></div><span>失败</span>';
        return;
      }

      ws.onopen = () => {
        capBrain.className = 'capsule cap-online';
        capBrain.innerHTML = '<div class="capsule-dot"></div><span>ESP32</span>';
        addChatMessage('系统: 与 ESP32-S3 大脑建立全双工 WebSocket 长连接成功！', 'robot');
      };

      ws.onclose = () => {
        capBrain.className = 'capsule cap-offline';
        capBrain.innerHTML = '<div class="capsule-dot"></div><span>断开</span>';
        updateChassisOnlineUI(false);
      };

      ws.onerror = () => {
        capBrain.className = 'capsule cap-offline';
        capBrain.innerHTML = '<div class="capsule-dot"></div><span>异常</span>';
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          handleServerMessage(msg);
        } catch (e) {}
      };
    }

    function reconnectWebSocket() {
      initWebSocket(wsUrlInput.value);
    }

    function updateChassisOnlineUI(online) {
      const capChassis = document.getElementById('capChassis');
      const stmStatusPill = document.getElementById('stmStatusPill');
      if (online) {
        capChassis.className = 'capsule cap-online';
        capChassis.innerHTML = '<div class="capsule-dot"></div><span>STM32</span>';
        if (stmStatusPill) {
          stmStatusPill.className = 'capsule cap-online';
          stmStatusPill.innerText = '在线 460.8k';
        }
      } else {
        capChassis.className = 'capsule cap-offline';
        capChassis.innerHTML = '<div class="capsule-dot"></div><span>STM32离线</span>';
        if (stmStatusPill) {
          stmStatusPill.className = 'capsule cap-offline';
          stmStatusPill.innerText = '底盘断开';
        }
      }
    }

    // ----------------------------------------------------
    // 3. 实时遥测数据渲染
    // ----------------------------------------------------
    let currentGimbalPan = 29.0;
    let currentGimbalTilt = 0.0;

    function handleServerMessage(msg) {
      if (msg.type !== 'telemetry') {
        if (msg.type === 'ai_reply') addChatMessage(msg.text, 'robot');
        return;
      }

      // 底盘链路
      if (msg.chassis_online !== undefined) {
        updateChassisOnlineUI(msg.chassis_online);
      }

      // 姿态俯仰角与微缩人工地平仪
      if (msg.pitch !== undefined) {
        const p = msg.pitch;
        document.getElementById('hudPitch').innerText = (p >= 0 ? '+' : '') + p.toFixed(1) + '°';
        document.getElementById('valPitch').innerText = (p >= 0 ? '+' : '') + p.toFixed(1) + '°';

        const bar = document.getElementById('miniHorizonBar');
        if (bar) {
          const ty = Math.max(-14, Math.min(14, p * 0.7));
          bar.style.transform = `translateY(${ty}px)`;
          bar.style.background = Math.abs(p) > 30 ? 'var(--red)' : (Math.abs(p) > 15 ? 'var(--yellow)' : 'linear-gradient(90deg, transparent, var(--cyan), transparent)');
        }
      }

      if (msg.roll !== undefined) document.getElementById('valRoll').innerText = (msg.roll >= 0 ? '+' : '') + msg.roll.toFixed(1) + '°';
      if (msg.pitch_rate !== undefined) document.getElementById('valPitchRate').innerText = msg.pitch_rate.toFixed(1) + ' °/s';
      if (msg.acc_pitch !== undefined) document.getElementById('valAccPitch').innerText = (msg.acc_pitch >= 0 ? '+' : '') + msg.acc_pitch.toFixed(1) + '°';

      // 车速与双轮动力学
      if (msg.speed !== undefined) {
        document.getElementById('hudSpeed').innerHTML = `${msg.speed} <small style="font-size:10px;">mm/s</small>`;
        document.getElementById('valSpeed').innerText = msg.speed + ' mm/s';
      }
      if (msg.l_spd !== undefined && msg.l_pulse !== undefined) {
        document.getElementById('valLeftWheel').innerText = `${msg.l_spd}mm/s (${msg.l_pulse}p)`;
      }
      if (msg.r_spd !== undefined && msg.r_pulse !== undefined) {
        document.getElementById('valRightWheel').innerText = `${msg.r_spd}mm/s (${msg.r_pulse}p)`;
      }
      if (msg.l_pwm !== undefined && msg.r_pwm !== undefined) {
        document.getElementById('valMotorPwm').innerText = `L:${msg.l_pwm} / R:${msg.r_pwm}`;
      }

      // 动力电池
      if (msg.battery_mv !== undefined) {
        const v = (msg.battery_mv / 1000.0).toFixed(2);
        const batPct = Math.max(0, Math.min(100, Math.round(((msg.battery_mv - 10500) / (12600 - 10500)) * 100)));
        document.getElementById('txtBat').innerText = v + 'V';
        document.getElementById('hudBatPct').innerText = batPct + '%';
        document.getElementById('valBattery').innerText = `${v} V (${batPct}%)`;
      }

      // 跌倒告警与安全标志
      if (msg.status_flags !== undefined) {
        const fall = (msg.status_flags & 0x02) !== 0;
        const calib = (msg.status_flags & 0x04) !== 0;
        const run = (msg.status_flags & 0x08) !== 0;
        document.getElementById('capAlarm').style.display = fall ? 'inline-flex' : 'none';
        document.getElementById('valChassisStatus').innerText = (calib ? 'IMU就绪' : '校准中') + ' | ' + (run ? '输出中' : '待机');
      }

      // STM32 芯片级存储与 UID
      if (msg.stm_uid) document.getElementById('valStmUid').innerText = msg.stm_uid;
      if (msg.stm_flash_tot && msg.stm_flash_used) {
        const tot = msg.stm_flash_tot, used = msg.stm_flash_used;
        const pct = Math.max(1, Math.min(100, Math.round((used / tot) * 100)));
        document.getElementById('valStmFlash').innerText = `${used}K / ${tot}K (${pct}%)`;
        const b = document.getElementById('barStmFlash'); if (b) b.style.width = pct + '%';
      }
      if (msg.stm_sram_tot && msg.stm_sram_free) {
        const tot = msg.stm_sram_tot, free = msg.stm_sram_free;
        const used = Math.max(0, tot - free);
        const pct = Math.max(1, Math.min(100, Math.round((used / tot) * 100)));
        document.getElementById('valStmSram').innerText = `堆余 ${free}K / 总 ${tot}K`;
        const b = document.getElementById('barStmSram'); if (b) b.style.width = pct + '%';
      }
      if (msg.rx_bytes && msg.rx_pkts) {
        document.getElementById('valChassisComm').innerText = `${(msg.rx_bytes/1024).toFixed(1)} KB / ${msg.rx_pkts} 帧`;
      }

      // ESP32 大脑指标
      if (msg.uptime) {
        const s = msg.uptime;
        const h = String(Math.floor(s / 3600)).padStart(2, '0');
        const m = String(Math.floor((s % 3600) / 60)).padStart(2, '0');
        const sec = String(s % 60).padStart(2, '0');
        document.getElementById('valUptime').innerText = `${h}:${m}:${sec}`;
      }
      if (msg.sram_free && msg.sram_min) {
        document.getElementById('valSram').innerText = `${msg.sram_free} / ${msg.sram_min} KB`;
      }
      if (msg.psram_free && msg.psram_total) {
        document.getElementById('valPsram').innerText = `${msg.psram_free} / ${msg.psram_total} KB`;
        const pct = Math.max(3, Math.min(100, Math.round(((msg.psram_total - msg.psram_free) / msg.psram_total) * 100)));
        document.getElementById('valPsramPct').innerText = pct + '%';
        const b = document.getElementById('barPsram'); if (b) b.style.width = pct + '%';
      }

      // 云台当前实际角度与雷达光标
      if (msg.g_pan !== undefined && msg.g_tilt !== undefined) {
        currentGimbalPan = msg.g_pan;
        currentGimbalTilt = msg.g_tilt;
        const pStr = msg.g_pan.toFixed(1);
        const tStr = msg.g_tilt.toFixed(1);
        document.getElementById('hudGimbal').innerText = `${Math.round(msg.g_pan)}° / ${Math.round(msg.g_tilt)}°`;
        document.getElementById('gimbalCurText').innerText = `${pStr}° / ${tStr}°`;
        document.getElementById('gimbalPanVal').innerText = pStr + '°';
        document.getElementById('gimbalTiltVal').innerText = tStr + '°';

        const dot = document.getElementById('gimbalRadarDot');
        if (dot) {
          const nx = Math.max(0, Math.min(100, (msg.g_pan / 180.0) * 100));
          const ny = Math.max(0, Math.min(100, 100 - (msg.g_tilt / 90.0) * 100));
          dot.style.left = nx + '%';
          dot.style.top = ny + '%';
        }
      }

      // 麦克风与自主模式
      if (msg.mic_energy !== undefined) {
        const ep = Math.min(100, Math.round(msg.mic_energy * 100));
        document.getElementById('valMicEnergy').innerText = ep + '%';
        const b = document.getElementById('barMicEnergy'); if (b) b.style.width = ep + '%';
      }
      if (msg.auto_mode !== undefined) {
        const t = document.getElementById('autoModeToggle');
        if (t && document.activeElement !== t) t.checked = msg.auto_mode;
      }
    }

    // ----------------------------------------------------
    // 4. 虚拟触控比例摇杆 (完全解耦底盘，绝不联动云台)
    // ----------------------------------------------------
    const joyBase = document.getElementById('joystickBase');
    const joyThumb = document.getElementById('joystickThumb');
    let isDraggingJoy = false;
    let joyTimer = null;
    let curSpeed = 0, curYaw = 0;

    function handleJoyMove(cx, cy) {
      const rect = joyBase.getBoundingClientRect();
      const centerX = rect.left + rect.width / 2;
      const centerY = rect.top + rect.height / 2;

      let dx = cx - centerX;
      let dy = cy - centerY;
      const maxR = (rect.width - 54) / 2;
      const dist = Math.sqrt(dx * dx + dy * dy);

      if (dist > maxR) {
        dx = (dx / dist) * maxR;
        dy = (dy / dist) * maxR;
      }

      joyThumb.style.transform = `translate(${dx}px, ${dy}px)`;

      const normX = dx / maxR;
      const normY = -dy / maxR;

      curSpeed = Math.round(normY * 250);  // 前正后负 mm/s
      curYaw = Math.round(-normX * 600);   // 左正右负 mrad/s

      document.getElementById('joyMetrics').innerText = `${curSpeed} mm/s | ${curYaw} mrad/s`;
    }

    function resetJoy() {
      isDraggingJoy = false;
      joyThumb.style.transform = 'translate(0px, 0px)';
      curSpeed = 0; curYaw = 0;
      document.getElementById('joyMetrics').innerText = '0 mm/s | 0 mrad/s';
      sendCmdVel(0, 0);
    }

    joyBase.addEventListener('touchstart', (e) => {
      e.preventDefault();
      isDraggingJoy = true;
      handleJoyMove(e.touches[0].clientX, e.touches[0].clientY);
      startJoyLoop();
    }, { passive: false });

    window.addEventListener('touchmove', (e) => {
      if (!isDraggingJoy) return;
      e.preventDefault();
      handleJoyMove(e.touches[0].clientX, e.touches[0].clientY);
    }, { passive: false });

    window.addEventListener('touchend', () => {
      if (isDraggingJoy) { stopJoyLoop(); resetJoy(); }
    });

    joyBase.addEventListener('mousedown', (e) => {
      isDraggingJoy = true;
      handleJoyMove(e.clientX, e.clientY);
      startJoyLoop();
    });

    window.addEventListener('mousemove', (e) => {
      if (!isDraggingJoy) return;
      handleJoyMove(e.clientX, e.clientY);
    });

    window.addEventListener('mouseup', () => {
      if (isDraggingJoy) { stopJoyLoop(); resetJoy(); }
    });

    function startJoyLoop() {
      if (joyTimer) clearInterval(joyTimer);
      joyTimer = setInterval(() => {
        if (isDraggingJoy) sendCmdVel(curSpeed, curYaw);
      }, 50);
    }

    function stopJoyLoop() {
      if (joyTimer) { clearInterval(joyTimer); joyTimer = null; }
    }

    function sendCmdVel(spd, yaw) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'cmd_vel', speed: spd, yaw: yaw }));
    }

    function sendCmd(action) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'cmd', action: action }));
    }

    // ----------------------------------------------------
    // 5. 云台独立控制与微调
    // ----------------------------------------------------
    function adjustGimbal(dPan, dTilt) {
      let p = Math.max(0, Math.min(180, Math.round(currentGimbalPan + dPan)));
      let t = Math.max(0, Math.min(90, Math.round(currentGimbalTilt + dTilt)));
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'gimbal_angle', pan: p, tilt: t }));
    }

    function sendGimbalGesture(gesture) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'gimbal_gesture', gesture: gesture }));
    }

    // ----------------------------------------------------
    // 6. 表情、音频与自主模式
    // ----------------------------------------------------
    function toggleAutoMode(enabled) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'autonomous', enabled: enabled }));
    }

    function setEmotion(state) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'emotion', state: state }));
    }

    function triggerDance() {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'dance' }));
    }

    function sendAudioCmd(action) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'audio', action: action }));
    }

    function onVolumeChange() {
      const vol = parseInt(document.getElementById('volumeSlider').value);
      document.getElementById('volumeVal').innerText = vol + '%';
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'audio', action: 'VOLUME', volume: vol / 100.0 }));
    }

    function sendAiMessage() {
      const input = document.getElementById('aiInput');
      const text = input.value.trim();
      if (!text) return;
      addChatMessage(text, 'user');
      input.value = '';
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'ai_prompt', text: text }));
      }
    }

    function addChatMessage(text, sender) {
      const chatLog = document.getElementById('chatLog');
      const div = document.createElement('div');
      div.className = `chat-msg ${sender}`;
      div.innerText = text;
      chatLog.appendChild(div);
      chatLog.scrollTop = chatLog.scrollHeight;
    }

    // 键盘 WASD 控制 (底盘完全独立，不联动云台)
    window.addEventListener('keydown', (e) => {
      if (document.activeElement.tagName === 'INPUT') return;
      if (e.repeat) return;
      if (e.key === 'w' || e.key === 'ArrowUp') sendCmd('FORWARD');
      else if (e.key === 's' || e.key === 'ArrowDown') sendCmd('BACKWARD');
      else if (e.key === 'a' || e.key === 'ArrowLeft') sendCmd('LEFT');
      else if (e.key === 'd' || e.key === 'ArrowRight') sendCmd('RIGHT');
      else if (e.key === ' ') sendCmd('STOP');
    });

    window.addEventListener('keyup', (e) => {
      if (document.activeElement.tagName === 'INPUT') return;
      if (['w', 's', 'a', 'd', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.key)) {
        sendCmd('STOP');
      }
    });

    window.onload = () => {
      initWebSocket(defaultHost ? `ws://${defaultHost}/ws` : 'ws://192.168.4.1/ws');
    };
  </script>
</body>
</html>
)rawliteral";
