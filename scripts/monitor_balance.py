#!/usr/bin/env python3
"""
实时监控自平衡小车姿态与电机动力输出
用法: python3 scripts/monitor_balance.py [IP]
默认 IP: 192.168.1.169
"""

import sys
import socket
import base64
import os
import json
import time

IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.169"
PORT = 80

print(f"==========================================================")
print(f"  自平衡小车 实时姿态与动力监控 (串流目标: {IP}:{PORT})")
print(f"  平衡零点基准: -1.8° (极性修正纯直立PD) | 安全保护: ±25.0°")
print(f"  扶正使能窗口: -5.8° ~ +2.2° (基准 ±4.0°)")
print(f"  按 Ctrl+C 退出监控")
print(f"==========================================================")

try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect((IP, PORT))

    key = base64.b64encode(os.urandom(16)).decode('ascii')
    req = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: {IP}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n\r\n"
    )
    s.sendall(req.encode('utf-8'))

    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(512)
        if not chunk:
            break
        buf += chunk

    header_end = buf.index(b"\r\n\r\n") + 4
    buf = buf[header_end:]

    print(f"WebSocket 连接就绪，开始流式接收底层遥测...")
    print(f"{'时间':^8} | {'俯仰角(Pitch)':^12} | {'角速度':^10} | {'左轮PWM':^9} | {'右轮PWM':^9} | {'平衡状态':^10}")
    print("-" * 72)

    while True:
        if len(buf) < 2:
            chunk = s.recv(512)
            if not chunk:
                break
            buf += chunk
            continue

        b1, b2 = buf[0], buf[1]
        payload_len = b2 & 0x7f
        offset = 2
        if payload_len == 126:
            if len(buf) < 4:
                buf += s.recv(512)
                continue
            payload_len = int.from_bytes(buf[2:4], 'big')
            offset = 4
        elif payload_len == 127:
            if len(buf) < 10:
                buf += s.recv(512)
                continue
            payload_len = int.from_bytes(buf[2:10], 'big')
            offset = 10

        total_len = offset + payload_len
        while len(buf) < total_len:
            chunk = s.recv(512)
            if not chunk:
                break
            buf += chunk

        payload = buf[offset:total_len]
        buf = buf[total_len:]

        try:
            d = json.loads(payload.decode('utf-8'))
            if d.get("type") != "telemetry":
                continue

            pitch = d.get("pitch", 0.0)
            rate = d.get("pitch_rate", 0.0)
            lpwm = d.get("l_pwm", 0)
            rpwm = d.get("r_pwm", 0)
            flags = d.get("status_flags", 0)
            is_fallen = bool(flags & 0x02)

            status_str = "【停机保护】" if is_fallen else "【平衡使能】"
            now_str = time.strftime("%H:%M:%S")

            print(f"{now_str:^8} | {pitch:10.2f}° | {rate:8.2f}°/s | {lpwm:8d} | {rpwm:8d} | {status_str:^10}")
        except Exception:
            pass

except KeyboardInterrupt:
    print("\n已退出监控。")
except Exception as e:
    print(f"\n连接中断: {e}")
finally:
    try:
        s.close()
    except Exception:
        pass
