#!/usr/bin/env python3
"""Luckfox上验证真实UART4↔C8；可额外经过真实桥进程，替代ESP的一端是PTY。

先停止S99zheadbridge，避免两个进程抢读UART。
所有STATE均enabled=False：只验证通信/表情，不使能舵机。
--through-bridge只能证明PTY→桥→UART4→C8，不代表ESP或UART3实线已验证。
"""
from __future__ import annotations

import argparse
import collections
import json
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import sys
import time

from head_bridge import open_port
from head_protocol import ACK, CAMERA, PING, STATE, STATUS, STATUS_FORMAT, Parser, encode, state_payload

STATUS_FIELDS = ("uptime_ms", "frames", "valid_frames", "crc_errors", "uart_errors",
                 "pan", "tilt", "target_pan", "target_tilt", "event_id", "emotion",
                 "flags", "last_sequence", "reserved", "firmware_version")


def run(through_bridge: bool) -> dict:
    proc = None
    slave = None
    report = {"path": "PTY -> Python bridge -> UART4 -> C8" if through_bridge else "UART4 -> C8",
              "esp32_and_uart3_wire_tested": False, "servo_enabled_by_test": False, "checks": []}
    fd = None
    try:
        if through_bridge:
            fd, slave = pty.openpty()
            proc = subprocess.Popen(
                [sys.executable, str(Path(__file__).with_name("head_bridge.py")),
                 "--esp", os.ttyname(slave), "--head", "/dev/ttyS4"],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if not select.select([proc.stderr], [], [], 5)[0]:
                raise TimeoutError("桥没有启动")
            startup = proc.stderr.readline().decode()
            if "已启动" not in startup:
                raise RuntimeError(startup)
        else:
            fd = open_port("/dev/ttyS4", 115200)
        parser = Parser()
        queue = collections.deque()
        statuses = []

        def wait(predicate, timeout=2.5):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                while queue:
                    frame = queue.popleft()
                    if frame.kind == STATUS:
                        statuses.append(dict(zip(STATUS_FIELDS, struct.unpack(STATUS_FORMAT, frame.payload))))
                    if predicate(frame):
                        return frame
                if proc is not None and proc.poll() is not None:
                    raise RuntimeError("桥提前退出")
                if select.select([fd], [], [], 0.05)[0]:
                    queue.extend(parser.feed(os.read(fd, 4096)))
            raise TimeoutError("没有收到期望的C8应答")

        def status_with(predicate):
            frame = wait(lambda f: f.kind == STATUS and predicate(
                dict(zip(STATUS_FIELDS, struct.unpack(STATUS_FORMAT, f.payload)))))
            return dict(zip(STATUS_FIELDS, struct.unpack(STATUS_FORMAT, frame.payload)))

        def check(name, condition=True):
            if not condition:
                raise AssertionError(name)
            report["checks"].append(name)

        os.write(fd, encode(PING, 207))
        first_frame = wait(lambda f: f.kind == STATUS and f.sequence == 207)
        baseline = dict(zip(STATUS_FIELDS, struct.unpack(STATUS_FORMAT, first_frame.payload)))
        report["initial_status"] = baseline
        check("PING返回完整CRC有效的STATUS")
        check("固件版本1.0", baseline["firmware_version"] == 0x100)

        for emotion in range(12):
            sequence = 40 + emotion
            os.write(fd, encode(STATE, sequence, state_payload(emotion=emotion, event=100 + emotion)))
            ack = wait(lambda f: f.kind == ACK and f.sequence == sequence)
            check(f"表情{emotion}命令ACK成功", ack.payload == bytes((STATE, 0)))
            status = status_with(lambda s: s["last_sequence"] == sequence and s["emotion"] == emotion)
            check(f"表情{emotion}状态回传且PWM关闭", (status["flags"] & 3) == 2)

        os.write(fd, encode(STATE, 120, state_payload(emotion=255)))
        rejected = wait(lambda f: f.kind == ACK and f.sequence == 120)
        check("非法表情被C8拒绝", rejected.payload == bytes((STATE, 2)))
        # 坏CRC帧在直接模式由C8丢弃；桥模式会在Linux先过滤掉。
        bad = bytearray(encode(STATE, 121, state_payload(emotion=2)))
        bad[-1] ^= 1
        os.write(fd, b"noise" + bad + encode(PING, 122))
        wait(lambda f: f.kind == STATUS and f.sequence == 122)
        check("坏CRC和噪声之后重新同步")
        os.write(fd, encode(STATE, 123, state_payload(emotion=1, event=200)))
        wait(lambda f: f.kind == ACK and f.sequence == 123)
        online = status_with(lambda s: s["last_sequence"] == 123 and s["emotion"] == 1)
        check("恢复NORMAL且保持PWM关闭", (online["flags"] & 3) == 2)
        final = status_with(lambda s: s["last_sequence"] == 123 and (s["flags"] & 3) == 0)
        check("停止STATE后心跳超时转离线", final["uptime_ms"] > online["uptime_ms"])
        check("测试期间无新增UART硬件错误", final["uart_errors"] == baseline["uart_errors"])
        expected_crc_delta = 0 if through_bridge else 1
        check("CRC错误计数符合过滤层级", final["crc_errors"] - baseline["crc_errors"] == expected_crc_delta)
        check("主机接收CRC错误为0", parser.crc_errors == 0)
        report.update(result="pass", final_status=final, received_status_count=len(statuses),
                      received_crc_errors=parser.crc_errors)
    except Exception as error:
        report.update(result="fail", error=str(error))
    finally:
        if proc is not None:
            proc.terminate()
            proc.communicate(timeout=5)
        if fd is not None:
            os.close(fd)
        if slave is not None:
            os.close(slave)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--through-bridge", action="store_true")
    args = parser.parse_args()
    report = run(args.through_bridge)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
