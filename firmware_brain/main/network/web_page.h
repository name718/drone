#pragma once

// 自动内嵌的前端 SPA 静态网页资源 (存储在 Flash 中，零外部文件系统依赖)
static const char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>CYBER-ROBOT // 大脑控制台</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(16, 24, 40, 0.75);
      --border: rgba(0, 240, 255, 0.2);
      --cyan: #00f0ff;
      --cyan-glow: rgba(0, 240, 255, 0.35);
      --green: #10b981;
      --red: #ef4444;
      --yellow: #f59e0b;
      --purple: #a855f7;
      --text: #e2e8f0;
      --text-muted: #94a3b8;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", sans-serif;
      -webkit-tap-highlight-color: transparent;
    }

    body {
      background: var(--bg);
      background-image: 
        radial-gradient(ellipse at top, rgba(0, 240, 255, 0.08), transparent 60%),
        radial-gradient(ellipse at bottom, rgba(168, 85, 247, 0.08), transparent 60%);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      padding: 16px;
      gap: 16px;
    }

    /* 顶部导航栏 */
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 18px;
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 12px;
      backdrop-filter: blur(12px);
    }

    .brand {
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .brand-logo {
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: var(--cyan);
      box-shadow: 0 0 10px var(--cyan);
      animation: pulse 2s infinite ease-in-out;
    }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(0.85); }
    }

    .brand-title {
      font-size: 16px;
      font-weight: 700;
      letter-spacing: 1px;
      color: #fff;
    }

    .conn-status {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 13px;
      padding: 4px 10px;
      border-radius: 20px;
      background: rgba(239, 68, 68, 0.15);
      color: var(--red);
      border: 1px solid rgba(239, 68, 68, 0.3);
      transition: all 0.3s ease;
    }

    .conn-status.connected {
      background: rgba(16, 185, 129, 0.15);
      color: var(--green);
      border-color: rgba(16, 185, 129, 0.3);
    }

    .conn-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: currentColor;
    }

    /* 独立连接地址栏 (方便本地电脑打开调试) */
    .ip-bar {
      display: flex;
      gap: 8px;
      background: var(--card-bg);
      padding: 8px 12px;
      border-radius: 10px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      align-items: center;
      font-size: 13px;
    }

    .ip-bar input {
      flex: 1;
      background: rgba(0, 0, 0, 0.4);
      border: 1px solid rgba(255, 255, 255, 0.1);
      color: #fff;
      padding: 6px 10px;
      border-radius: 6px;
      outline: none;
      font-family: monospace;
      font-size: 13px;
    }

    .ip-bar input:focus {
      border-color: var(--cyan);
    }

    .ip-bar button {
      background: var(--cyan);
      color: #000;
      border: none;
      font-weight: 600;
      padding: 6px 14px;
      border-radius: 6px;
      cursor: pointer;
      transition: opacity 0.2s;
    }

    .ip-bar button:active {
      opacity: 0.8;
    }

    /* 网格布局 */
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
      gap: 16px;
    }

    .card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 16px;
      backdrop-filter: blur(12px);
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    .card-title {
      font-size: 14px;
      font-weight: 600;
      color: var(--cyan);
      display: flex;
      align-items: center;
      gap: 8px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      padding-bottom: 8px;
    }

    /* 数据列表项 */
    .stat-list {
      display: flex;
      flex-direction: column;
      gap: 10px;
    }

    .stat-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 13px;
    }

    .stat-label {
      color: var(--text-muted);
    }

    .stat-val {
      font-family: monospace;
      font-weight: 600;
      color: #fff;
    }

    .progress-bar-bg {
      width: 100%;
      height: 6px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 3px;
      overflow: hidden;
      margin-top: 4px;
    }

    .progress-bar-fill {
      height: 100%;
      background: linear-gradient(90deg, var(--cyan), var(--purple));
      width: 0%;
      transition: width 0.4s ease;
    }

    /* 遥控手柄面板 */
    .controller-box {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 10px 0;
      gap: 10px;
    }

    .ctrl-row {
      display: flex;
      gap: 12px;
    }

    .ctrl-btn {
      width: 72px;
      height: 64px;
      border: 1px solid rgba(0, 240, 255, 0.3);
      background: rgba(0, 240, 255, 0.06);
      color: var(--cyan);
      font-size: 20px;
      font-weight: 700;
      border-radius: 12px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 4px;
      cursor: pointer;
      user-select: none;
      transition: all 0.15s ease;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    }

    .ctrl-btn span {
      font-size: 11px;
      font-weight: 500;
      color: var(--text-muted);
    }

    .ctrl-btn:active, .ctrl-btn.active {
      transform: scale(0.92);
      background: var(--cyan);
      color: #000;
      border-color: var(--cyan);
      box-shadow: 0 0 16px var(--cyan);
    }

    .ctrl-btn:active span, .ctrl-btn.active span {
      color: #000;
    }

    .ctrl-btn.stop {
      border-color: rgba(239, 68, 68, 0.4);
      background: rgba(239, 68, 68, 0.1);
      color: var(--red);
    }

    .ctrl-btn.stop:active, .ctrl-btn.stop.active {
      background: var(--red);
      color: #fff;
      box-shadow: 0 0 16px var(--red);
    }

    /* 表情与动作网格按钮 */
    .btn-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 10px;
    }

    .action-btn {
      padding: 10px 14px;
      border: 1px solid rgba(0, 240, 255, 0.25);
      background: rgba(0, 240, 255, 0.05);
      color: #fff;
      border-radius: 10px;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      transition: all 0.2s ease;
      user-select: none;
    }

    .action-btn:hover {
      background: rgba(0, 240, 255, 0.15);
      border-color: var(--cyan);
    }

    .action-btn:active, .action-btn.active {
      transform: scale(0.95);
      background: var(--cyan);
      color: #000;
      box-shadow: 0 0 14px var(--cyan);
    }

    .action-btn.purple {
      border-color: rgba(168, 85, 247, 0.3);
      background: rgba(168, 85, 247, 0.06);
    }
    .action-btn.purple:hover {
      border-color: var(--purple);
      background: rgba(168, 85, 247, 0.18);
    }
    .action-btn.purple:active {
      background: var(--purple);
      color: #fff;
      box-shadow: 0 0 14px var(--purple);
    }

    /* 舵机角度滑块 */
    .slider-box {
      display: flex;
      flex-direction: column;
      gap: 10px;
      padding: 4px 0;
    }

    .slider-row {
      display: flex;
      align-items: center;
      gap: 10px;
      font-size: 13px;
    }

    .slider-label {
      width: 70px;
      color: var(--text-muted);
    }

    .slider-val {
      width: 38px;
      font-family: monospace;
      color: var(--cyan);
      font-weight: 600;
      text-align: right;
    }

    .slider-input {
      flex: 1;
      accent-color: var(--cyan);
      cursor: pointer;
    }

    /* AI 大模型交互视窗 */
    .ai-box {
      display: flex;
      flex-direction: column;
      gap: 10px;
      height: 100%;
    }

    .chat-log {
      flex: 1;
      min-height: 140px;
      max-height: 200px;
      overflow-y: auto;
      background: rgba(0, 0, 0, 0.4);
      border-radius: 8px;
      padding: 10px;
      display: flex;
      flex-direction: column;
      gap: 8px;
      font-size: 13px;
      border: 1px solid rgba(255, 255, 255, 0.05);
    }

    .chat-msg {
      padding: 6px 10px;
      border-radius: 8px;
      max-width: 85%;
      word-break: break-word;
      line-height: 1.4;
    }

    .chat-msg.user {
      align-self: flex-end;
      background: rgba(168, 85, 247, 0.25);
      border: 1px solid rgba(168, 85, 247, 0.4);
      color: #f1f5f9;
    }

    .chat-msg.robot {
      align-self: flex-start;
      background: rgba(0, 240, 255, 0.15);
      border: 1px solid rgba(0, 240, 255, 0.3);
      color: #38bdf8;
    }

    .chat-input-row {
      display: flex;
      gap: 8px;
    }

    .chat-input-row input {
      flex: 1;
      background: rgba(0, 0, 0, 0.3);
      border: 1px solid rgba(255, 255, 255, 0.1);
      color: #fff;
      padding: 8px 12px;
      border-radius: 8px;
      outline: none;
      font-size: 13px;
    }

    .chat-input-row input:focus {
      border-color: var(--purple);
    }

    .chat-input-row button {
      background: var(--purple);
      color: #fff;
      border: none;
      padding: 0 16px;
      border-radius: 8px;
      cursor: pointer;
      font-weight: 600;
      transition: opacity 0.2s;
    }

    .chat-input-row button:active {
      opacity: 0.8;
    }
  </style>
</head>
<body>

  <!-- 头部状态栏 -->
  <header>
    <div class="brand">
      <div class="brand-logo"></div>
      <div class="brand-title">CYBER-ROBOT // BRAIN</div>
    </div>
    <div class="conn-status" id="connStatus">
      <div class="conn-dot"></div>
      <span id="connText">未连接</span>
    </div>
  </header>

  <!-- WebSocket 目标地址输入条 -->
  <div class="ip-bar">
    <span style="color: var(--text-muted);">WS 地址:</span>
    <input type="text" id="wsUrlInput" placeholder="ws://192.168.x.x/ws">
    <button onclick="reconnectWebSocket()">连接</button>
  </div>

  <!-- 主体卡片网格 -->
  <div class="grid">
    
    <!-- 卡片 1: 硬件与内存指标 -->
    <div class="card">
      <div class="card-title">
        <span>🧠 硬件与内存实时体检</span>
      </div>
      <div class="stat-list">
        <div class="stat-item">
          <span class="stat-label">芯片型号</span>
          <span class="stat-val" id="valChip">ESP32-S3 (240MHz)</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">运行时间</span>
          <span class="stat-val" id="valUptime">00:00:00</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">内部 SRAM 剩余</span>
          <span class="stat-val" id="valSram">-- KB</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">八线 PSRAM 剩余</span>
          <span class="stat-val" id="valPsram">-- KB</span>
        </div>
        <div>
          <div class="stat-item">
            <span class="stat-label">内存健康占用率</span>
            <span class="stat-val" id="valMemPercent">0%</span>
          </div>
          <div class="progress-bar-bg">
            <div class="progress-bar-fill" id="memBar"></div>
          </div>
        </div>
      </div>
    </div>

    <!-- 卡片 2: 运动遥控手柄 -->
    <div class="card">
      <div class="card-title">
        <span>🎮 移动底盘控制中枢 (按键/WASD)</span>
      </div>
      <div class="controller-box">
        <div class="ctrl-row">
          <button class="ctrl-btn" onmousedown="sendCmd('FORWARD')" onmouseup="sendCmd('STOP')"
                  ontouchstart="sendCmd('FORWARD')" ontouchend="sendCmd('STOP')">
            ▲<span>前进</span>
          </button>
        </div>
        <div class="ctrl-row">
          <button class="ctrl-btn" onmousedown="sendCmd('LEFT')" onmouseup="sendCmd('STOP')"
                  ontouchstart="sendCmd('LEFT')" ontouchend="sendCmd('STOP')">
            ◀<span>左转</span>
          </button>
          <button class="ctrl-btn stop" onclick="sendCmd('STOP')">
            ■<span>急停</span>
          </button>
          <button class="ctrl-btn" onmousedown="sendCmd('RIGHT')" onmouseup="sendCmd('STOP')"
                  ontouchstart="sendCmd('RIGHT')" ontouchend="sendCmd('STOP')">
            ▶<span>右转</span>
          </button>
        </div>
        <div class="ctrl-row">
          <button class="ctrl-btn" onmousedown="sendCmd('BACKWARD')" onmouseup="sendCmd('STOP')"
                  ontouchstart="sendCmd('BACKWARD')" ontouchend="sendCmd('STOP')">
            ▼<span>后退</span>
          </button>
        </div>
      </div>
    </div>

    <!-- 卡片 3: 拟人赛博表情中枢 -->
    <div class="card">
      <div class="card-title">
        <span>🎭 赛博拟人表情联动 (Face Engine)</span>
      </div>
      <div class="btn-grid">
        <button class="action-btn" onclick="setEmotion('NORMAL')">
          🤖 正常眨眼
        </button>
        <button class="action-btn" onclick="setEmotion('HAPPY')">
          😄 开心月牙
        </button>
        <button class="action-btn" onclick="setEmotion('SURPRISED')">
          😲 震惊大眼
        </button>
        <button class="action-btn" onclick="setEmotion('SLEEPY')">
          😴 困倦打盹
        </button>
      </div>
    </div>

    <!-- 卡片 4: 二自由度头部云台控制 -->
    <div class="card">
      <div class="card-title">
        <span>🦾 二自由度头部云台 (SG90 舵机)</span>
      </div>
      <div class="btn-grid" style="margin-bottom: 8px;">
        <button class="action-btn purple" onclick="sendGimbalGesture('NOD')">
          🙆 点头认同
        </button>
        <button class="action-btn purple" onclick="sendGimbalGesture('SHAKE')">
          🙅 摇头拒绝
        </button>
        <button class="action-btn purple" style="grid-column: 1 / -1;" onclick="sendGimbalGesture('RESET')">
          🎯 回正居中 (29°, 0°)
        </button>
      </div>
      <div class="slider-box">
        <div class="slider-row">
          <span class="slider-label">水平 Pan:</span>
          <input type="range" class="slider-input" id="panSlider" min="0" max="180" value="29" oninput="onGimbalSliderChange()">
          <span class="slider-val" id="panVal">29°</span>
        </div>
        <div class="slider-row">
          <span class="slider-label">俯仰 Tilt:</span>
          <input type="range" class="slider-input" id="tiltSlider" min="0" max="90" value="0" oninput="onGimbalSliderChange()">
          <span class="slider-val" id="tiltVal">0°</span>
        </div>
      </div>
    </div>

    <!-- 卡片 5: AI 大模型对话视窗 (预留双向流式通道) -->
    <div class="card" style="grid-column: 1 / -1;">
      <div class="card-title">
        <span>💬 AI 智脑流式交互通道 (WebSocket 全双工)</span>
      </div>
      <div class="ai-box">
        <div class="chat-log" id="chatLog">
          <div class="chat-msg robot">你好！我是你的桌面赛博机器人大脑。WebSocket 通道已开启！</div>
        </div>
        <div class="chat-input-row">
          <input type="text" id="aiInput" placeholder="输入提示词或与大模型对话..." onkeydown="if(event.key==='Enter') sendAiMessage()">
          <button onclick="sendAiMessage()">发送</button>
        </div>
      </div>
    </div>

  </div>

  <script>
    // 自动适配 WebSocket 地址 (如果是通过 ESP32 访问，自动使用当前 Host)
    let ws = null;
    const wsUrlInput = document.getElementById('wsUrlInput');
    const defaultHost = window.location.host || '192.168.1.100';
    const defaultUrl = `ws://${defaultHost}/ws`;
    wsUrlInput.value = defaultUrl;

    function initWebSocket(url) {
      if (ws) {
        ws.close();
      }

      const connStatus = document.getElementById('connStatus');
      const connText = document.getElementById('connText');

      connStatus.className = 'conn-status';
      connText.innerText = '正在连接...';

      try {
        ws = new WebSocket(url);
      } catch (e) {
        connText.innerText = '连接失败';
        return;
      }

      ws.onopen = () => {
        connStatus.className = 'conn-status connected';
        connText.innerText = '在线连接';
        addChatMessage('系统: 与 ESP32-S3 大脑建立长连接成功！', 'robot');
      };

      ws.onclose = () => {
        connStatus.className = 'conn-status';
        connText.innerText = '连接断开';
      };

      ws.onerror = (err) => {
        connStatus.className = 'conn-status';
        connText.innerText = '连接异常';
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          handleServerMessage(msg);
        } catch (e) {
          console.log('接收到非 JSON 数据:', event.data);
        }
      };
    }

    function reconnectWebSocket() {
      initWebSocket(wsUrlInput.value);
    }

    // 处理来自 ESP32 的 WebSocket 消息
    function handleServerMessage(msg) {
      if (msg.type === 'telemetry') {
        // 更新硬件指标
        if (msg.uptime !== undefined) {
          const s = msg.uptime;
          const h = String(Math.floor(s / 3600)).padStart(2, '0');
          const m = String(Math.floor((s % 3600) / 60)).padStart(2, '0');
          const sec = String(s % 60).padStart(2, '0');
          document.getElementById('valUptime').innerText = `${h}:${m}:${sec}`;
        }
        if (msg.sram_kb !== undefined) {
          document.getElementById('valSram').innerText = msg.sram_kb + ' KB';
        }
        if (msg.psram_kb !== undefined) {
          document.getElementById('valPsram').innerText = msg.psram_kb + ' KB';
          const totalPsram = 8192;
          const used = totalPsram - msg.psram_kb;
          const pct = Math.max(5, Math.min(100, Math.round((used / totalPsram) * 100)));
          document.getElementById('valMemPercent').innerText = pct + '%';
          document.getElementById('memBar').style.width = pct + '%';
        }
      } else if (msg.type === 'ai_reply') {
        // 大模型回复
        addChatMessage(msg.text, 'robot');
      }
    }

    // 发送运动控制指令
    function sendCmd(action) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      const payload = JSON.stringify({ type: 'cmd', action: action });
      ws.send(payload);
    }

    // 🎭 切换表情
    function setEmotion(state) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'emotion', state: state }));
    }

    // 🦾 云台预设手势
    function sendGimbalGesture(gesture) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'gimbal_gesture', gesture: gesture }));
      if (gesture === 'RESET') {
        document.getElementById('panSlider').value = 29;
        document.getElementById('panVal').innerText = '29°';
        document.getElementById('tiltSlider').value = 0;
        document.getElementById('tiltVal').innerText = '0°';
      }
    }

    // 🦾 云台滑条拖动
    function onGimbalSliderChange() {
      const pan = parseInt(document.getElementById('panSlider').value);
      const tilt = parseInt(document.getElementById('tiltSlider').value);
      document.getElementById('panVal').innerText = pan + '°';
      document.getElementById('tiltVal').innerText = tilt + '°';
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify({ type: 'gimbal_angle', pan: pan, tilt: tilt }));
    }

    // 发送大模型消息
    function sendAiMessage() {
      const input = document.getElementById('aiInput');
      const text = input.value.trim();
      if (!text) return;

      addChatMessage(text, 'user');
      input.value = '';

      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'ai_prompt', text: text }));
      } else {
        addChatMessage('错误: WebSocket 尚未连接，无法上送大脑！', 'robot');
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

    // 键盘 WASD / 方向键 快捷控制
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

    // 启动初始连接
    window.onload = () => {
      initWebSocket(defaultUrl);
    };
  </script>
</body>
</html>
)rawliteral";
