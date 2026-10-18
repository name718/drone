#!/usr/bin/env python3
"""经ESP网页下发其支持的8种表情，用运行中的SWD只读诊断测量差量刷新。

不暂停CPU，不注入SWD控制帧；测试时舵机动力电源应断开。
FPS由完整帧计数增量计算；耗时和字节数为每50ms采样的近似分布，非逻辑分析仪记录。
结束恢复NORMAL、29/0及关闭自主动作。
"""
import argparse
import hashlib
import json
import selectors
import subprocess
import threading
import time
from pathlib import Path

from head_swd_test import FIELDS, ROOT, Swd, symbols

# 现有网页接口没有BOOTING/THINKING/WINK/COOL命令，不能把未下发的表情算作实测。
EMOTIONS = ['NORMAL', 'HAPPY', 'SURPRISED', 'SLEEPY', 'LOVE', 'ANGRY', 'CONFUSED', 'DIZZY']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', required=True)
    parser.add_argument('--seconds', type=float, default=3)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('--seconds must be >= 1')
    syms = symbols(ROOT / 'firmware_head/build/firmware_head.elf')
    swd = Swd(6666)
    child = subprocess.Popen(['node', str(ROOT / 'tools/head_websocket_probe.mjs'), f'ws://{args.ip}/ws'],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    report = {'measurement': 'real C8 frame counter; sampled frame time and SPI bytes',
              'spi_hz': 1125000, 'mechanical_servo_motion_verified': False, 'emotions': {}}

    def send(value):
        child.stdin.write(json.dumps(value) + '\n')
        child.stdin.flush()

    def diag():
        return dict(zip(FIELDS, swd.read(syms['g_head_diag'], 32, len(FIELDS))))

    try:
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            if not selector.select(10) or json.loads(child.stdout.readline()).get('probe') != 'connected':
                raise RuntimeError('ESP WebSocket did not connect')
        # 持续消费遥测，防止管道写满反过来阻塞WebSocket接收。
        def consume_telemetry():
            for _ in child.stdout:
                pass
        drain = threading.Thread(target=consume_telemetry, daemon=True)
        drain.start()
        send({'type': 'autonomous', 'enable': False})
        for emotion, name in enumerate(EMOTIONS, start=1):
            send({'type': 'emotion', 'emotion': name})
            deadline = time.monotonic() + 5
            while True:
                value = diag()
                if value['emotion'] == emotion and value['link_online']:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError(f'{name} did not reach C8')
                time.sleep(.05)
            # 不计切换时的一次大面积重绘；持续动画仍包含呼吸、眨眼及声浪。
            time.sleep(.5)
            first = diag()
            started = time.monotonic()
            samples, previous = [], first['frame_count']
            while time.monotonic() - started < args.seconds:
                value = diag()
                if value['frame_count'] != previous:
                    samples.append({'ms': value['frame_time_ms'],
                                    'bytes': swd.read(syms['g_lcd_transfer_bytes'], 32, 1)[0]})
                    previous = value['frame_count']
                time.sleep(.05)
            last, elapsed = diag(), time.monotonic() - started
            if not samples or last['frame_count'] == first['frame_count']:
                raise AssertionError(f'{name}: no frames')
            for field in ('spi_errors', 'uart_errors', 'crc_errors', 'last_fault'):
                if last[field] != first[field] or (field in ('spi_errors', 'last_fault') and last[field]):
                    raise AssertionError(f'{name}: {field}')
            times = sorted(s['ms'] for s in samples)
            result = {'fps': round((last['frame_count'] - first['frame_count']) / elapsed, 2),
                      'frames': last['frame_count'] - first['frame_count'],
                      'seconds': round(elapsed, 3), 'samples': len(samples),
                      'sampled_frame_ms_median': times[len(times) // 2],
                      'sampled_frame_ms_max': max(times),
                      'sampled_spi_bytes_mean': round(sum(s['bytes'] for s in samples) / len(samples)),
                      'sampled_spi_bytes_max': max(s['bytes'] for s in samples)}
            report['emotions'][name] = result
            print(name, json.dumps(result), flush=True)
        report['result'] = 'pass'
    except Exception as error:
        report.update(result='fail', error=str(error))
        raise
    finally:
        if child.poll() is None:
            send({'type': 'autonomous', 'enable': False})
            send({'type': 'emotion', 'emotion': 'NORMAL'})
            send({'type': 'gimbal_angle', 'pan': 29, 'tilt': 0})
            time.sleep(.5)
        child.terminate()
        child.wait(timeout=5)
        swd.socket.close()
        report['c8_firmware_sha256'] = hashlib.sha256((ROOT / 'firmware_head/build/firmware_head.bin').read_bytes()).hexdigest()
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
