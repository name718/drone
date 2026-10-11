#!/usr/bin/env python3
"""
自平衡小车 物理零点测量与实时校准工具
通过 SWD (OpenOCD) 免复位直读与实时调参
"""

import subprocess
import time
import socket
import struct
import sys

print("==========================================================")
print("  自平衡小车 物理重心零点实时采样与校准")
print("==========================================================")
print("正在通过 ST-Link 建立 SWD 实时通信通道...")

openocd_proc = subprocess.Popen(
    ['openocd', '-f', 'interface/stlink.cfg', '-f', 'target/stm32g4x.cfg'],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL
)

time.sleep(1.2)

try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 4444))
    s.recv(1024)

    def read_mem_float(addr):
        cmd = f"mdw {addr} 1\n".encode()
        s.sendall(cmd)
        time.sleep(0.04)
        resp = s.recv(1024).decode('utf-8', errors='ignore')
        for line in resp.splitlines():
            if f"{addr}:" in line or "0x" in line:
                parts = line.split()
                for p in parts:
                    if len(p) == 8:
                        try:
                            w = int(p, 16)
                            return struct.unpack('<f', struct.pack('<I', w))[0]
                        except:
                            pass
        return None

    def write_mem_float(addr, val):
        w = struct.unpack('<I', struct.pack('<f', val))[0]
        cmd = f"mww {addr} 0x{w:08x}\n".encode()
        s.sendall(cmd)
        time.sleep(0.04)
        s.recv(1024)

    cur_pitch = read_mem_float("0x20000240")
    cur_tgt = read_mem_float("0x20000264")
    cur_kp = read_mem_float("0x2000025c")
    cur_kd = read_mem_float("0x20000260")

    print(f"当前 RAM 读数:")
    print(f"  当前俯仰角 (Pitch)      : {cur_pitch:.2f}°" if cur_pitch is not None else "  当前俯仰角: 读取中")
    print(f"  当前平衡中值 (Target)   : {cur_tgt:.2f}°" if cur_tgt is not None else "  当前中值: 读取中")
    print(f"  当前直立刚度 (Kp)       : {cur_kp:.1f}" if cur_kp is not None else "")
    print(f"  当前直立阻尼 (Kd)       : {cur_kd:.2f}" if cur_kd is not None else "")

    if len(sys.argv) > 1 and sys.argv[1] == "sample":
        print("\n>>> 开始连续采样 3 秒，请将小车手扶垂直保持静止...")
        samples = []
        for i in range(15):
            p = read_mem_float("0x20000240")
            if p is not None:
                samples.append(p)
                print(f"  采样 [{i+1:02d}/15]: Pitch = {p:6.2f}°")
            time.sleep(0.2)

        if samples:
            avg_pitch = sum(samples) / len(samples)
            print(f"\n>>> 测量完成！小车物理垂直中位均值 = {avg_pitch:.2f}°")
            print(f">>> 正在将 TargetPitch 实时写入 STM32 RAM: {avg_pitch:.2f}°...")
            write_mem_float("0x20000264", avg_pitch)
            new_tgt = read_mem_float("0x20000264")
            print(f">>> 写入成功！当前运行生效 TargetPitch = {new_tgt:.2f}°！")
    elif len(sys.argv) > 1 and sys.argv[1] == "set":
        new_val = float(sys.argv[2])
        print(f">>> 正在写入指定 TargetPitch: {new_val:.2f}°...")
        write_mem_float("0x20000264", new_val)
        print(f">>> 写入成功！当前运行生效 TargetPitch = {read_mem_float('0x20000264'):.2f}°")

    s.sendall(b"shutdown\n")
    s.close()

except Exception as e:
    print(f"通信异常: {e}")
finally:
    openocd_proc.terminate()
    try:
        openocd_proc.wait(timeout=2)
    except:
        openocd_proc.kill()
