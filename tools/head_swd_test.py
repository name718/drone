#!/usr/bin/env python3
"""经OpenOCD本地TCL接口测试真实C8板；不冒充物理UART联调。

先启动仅绑定127.0.0.1的OpenOCD，再运行本脚本。默认禁止舵机动作；
显式 --motion 仅测试29°/0°基准附近1°动作，完成后关闭PWM。
"""
from __future__ import annotations
import argparse
import hashlib
import json
import socket
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/luckfox"))
from head_protocol import ACK, CAMERA, PING, STATE, STATUS, Parser, encode, state_payload

FIELDS = """magic version uptime_ms boot_count reset_flags core_hz
frame_count frame_time_ms max_frame_ms spi_errors
uart_rx_bytes uart_tx_bytes uart_errors tx_drops
uart_valid_frames swd_valid_frames crc_errors header_errors
commands_applied commands_rejected
pan_cdeg tilt_cdeg target_pan_cdeg target_tilt_cdeg
servo_enabled link_online emotion energy event_id camera_ready
pwm_pan_us pwm_tilt_us loop_count last_fault last_sequence gpioa_idr gpiob_idr""".split()


def symbols(path: Path) -> dict[str, int]:
    """读取ELF32符号表，无需在Mac额外安装ARM gdb或pyelftools。"""
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("需要32位小端ELF")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    section_offset, section_size, section_count = header[6], header[11], header[12]
    sections = [struct.unpack_from("<IIIIIIIIII", data, section_offset + i * section_size)
                for i in range(section_count)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, _size, _info, _other, _index = struct.unpack_from("<IIIBBH", data, offset)
            end = strings.find(b"\0", name)
            result[strings[name:end].decode()] = value
    return result


class Swd:
    def __init__(self, port: int):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)

    def command(self, text: str) -> str:
        self.socket.sendall(text.encode() + b"\x1a")
        response = bytearray()
        while True:
            value = self.socket.recv(65536)
            if not value:
                raise ConnectionError("OpenOCD连接关闭")
            response.extend(value)
            if response.endswith(b"\x1a"):
                return response[:-1].decode().strip()

    def read(self, address: int, width: int, count: int) -> list[int]:
        answer = self.command(f"read_memory 0x{address:x} {width} {count}")
        try:
            values = [int(value, 0) for value in answer.split()]
        except ValueError as error:
            raise RuntimeError(f"内存读取失败: {answer}") from error
        if len(values) != count:
            raise RuntimeError(f"内存读取数量错误: {answer}")
        return values

    def write(self, address: int, width: int, values) -> None:
        result = self.command(f"write_memory 0x{address:x} {width} {{{' '.join(map(str, values))}}}")
        if result:
            raise RuntimeError(f"内存写入失败: {result}")

    def close(self):
        self.command("resume")
        self.socket.close()


def run(args) -> dict:
    syms = symbols(ROOT / "firmware_head/build/firmware_head.elf")
    swd = Swd(args.port)
    report = {"transport": "SWD mailbox, not physical UART", "checks": []}

    def check(condition, name):
        if not condition:
            raise AssertionError(name)
        report["checks"].append(name)
        print("PASS", name, flush=True)

    def diag():
        swd.command("halt")
        try:
            return dict(zip(FIELDS, swd.read(syms["g_head_diag"], 32, len(FIELDS))))
        finally:
            swd.command("resume")

    def inject(wire, wait=0.08):
        swd.command("halt")
        try:
            swd.write(syms["g_swd_reply_length"], 32, [0])
            swd.write(syms["g_swd_frame"], 8, wire)
            swd.write(syms["g_swd_length"], 32, [len(wire)])
        finally:
            swd.command("resume")
        time.sleep(wait)

    def response():
        swd.command("halt")
        try:
            n = swd.read(syms["g_swd_reply_length"], 32, 1)[0]
            if not 0 < n <= 72:
                raise AssertionError(f"没有SWD响应，length={n}")
            wire = bytes(swd.read(syms["g_swd_reply"], 8, n))
            frame = Parser().feed(wire)
            if len(frame) != 1:
                raise AssertionError("响应CRC或帧格式错误")
            return frame[0]
        finally:
            swd.command("resume")

    def state(sequence, **kwargs):
        inject(encode(STATE, sequence, state_payload(**kwargs)))

    def wait_diag(predicate, timeout=1.0):
        # 协议接收与一帧动画完成是两件事；不能用固定80ms猜测DIZZY等复杂帧已完成。
        until = time.monotonic() + timeout
        while True:
            current = diag()
            if predicate(current) or time.monotonic() >= until:
                return current
            time.sleep(0.02)

    try:
        time.sleep(0.3)
        before = diag()
        report["initial"] = before
        check(before["magic"] == 0x48454144, "firmware diagnostic signature")
        check(before["core_hz"] == 72000000, "8MHz HSE -> 72MHz core")
        check(before["last_fault"] == 0, "no recorded HardFault")
        check(before["servo_enabled"] == 0, "PWM disabled without host command")
        time.sleep(0.5)
        after = diag()
        check(after["boot_count"] == before["boot_count"], "no watchdog reset during rendering")
        check(after["frame_count"] > before["frame_count"] + 4, "screen frames advance on real MCU")
        check(after["spi_errors"] == 0, "SPI/DMA transfers finish without timeout")
        inject(encode(PING, 7))
        ping = response()
        check(ping.kind == STATUS and ping.sequence == 7, "PING receives CRC-valid status")
        for emotion in range(1, 12):
            state(10 + emotion, emotion=emotion, energy=150, event=emotion)
            check(wait_diag(lambda d: d["emotion"] == emotion)["emotion"] == emotion,
                  f"emotion {emotion} applied")
        state(30, emotion=0, event=30)
        check(wait_diag(lambda d: d["emotion"] == 0)["emotion"] == 0,
              "boot animation can be requested again")
        last = diag()
        invalid = encode(STATE, 31, state_payload(emotion=99))
        inject(invalid)
        answer = response()
        check(answer.kind == ACK and answer.payload == bytes((STATE, 2)), "invalid emotion rejected atomically")
        broken = bytearray(encode(STATE, 32, state_payload(emotion=6)))
        broken[-1] ^= 1
        inject(broken)
        check(diag()["crc_errors"] == last["crc_errors"] + 1, "bad CRC rejected on real MCU")
        state(33, emotion=1, pan=18000, tilt=18000, event=33)
        limited = diag()
        check(limited["target_pan_cdeg"] == 5200 and limited["target_tilt_cdeg"] == 2500,
              "mechanical limits applied while PWM remains off")
        check(limited["servo_enabled"] == 0, "limit test does not move servos")
        inject(encode(CAMERA, 34, b"\x01"))
        camera = diag()
        check(camera["camera_ready"] == 1 and camera["gpiob_idr"] & (1 << 11) and not camera["gpiob_idr"] & (1 << 12),
              "camera wake sequence: PB11 high / PB12 low")
        inject(encode(CAMERA, 35, b"\x00"))
        camera = diag()
        check(camera["camera_ready"] == 0 and not camera["gpiob_idr"] & (1 << 11) and camera["gpiob_idr"] & (1 << 12),
              "camera sleep sequence: PB11 low / PB12 high")
        if args.motion:
            state(40, emotion=1, enabled=True, event=40)
            swd.command("halt")
            try:
                timer = swd.read(0x40000000, 32, 15)
            finally:
                swd.command("resume")
            check(timer[10] == 71 and timer[11] == 19999 and timer[8] == 0x11,
                  "TIM2 1MHz counter / 20000us frame / both outputs enabled")
            check(timer[13] == 822 and timer[14] == 500, "calibrated home PWM: pan822us / tilt500us")
            state(41, emotion=1, enabled=True, pan=3000, tilt=100, event=41)
            for _ in range(5):
                state(41, emotion=1, enabled=True, pan=3000, tilt=100, event=41)
            moved = diag()
            check(2900 < moved["pan_cdeg"] <= 3000 and 0 < moved["tilt_cdeg"] <= 100,
                  "smooth 1-degree motion command")
            for _ in range(5):
                state(42, emotion=1, enabled=True, event=42)
            time.sleep(1.1)
            check(diag()["servo_enabled"] == 0, "one-second communication timeout disables PWM")
        state(50, emotion=1, enabled=False, event=50)
        report["final"] = diag()
        check(report["final"]["servo_enabled"] == 0 and report["final"]["last_fault"] == 0,
              "test ends with PWM disabled and no HardFault")
        check(report["final"]["spi_errors"] == 0, "no SPI errors across all emotion tests")
        report["firmware_sha256"] = hashlib.sha256((ROOT / "firmware_head/build/firmware_head.bin").read_bytes()).hexdigest()
        final = report["final"]
        report["physical_uart"] = {
            "verified_end_to_end": False,
            "valid_uart_frames": final["uart_valid_frames"],
            "uart_error_count": final["uart_errors"],
            "rx_pin_high": bool(final["gpioa_idr"] & (1 << 10)),
        }
        report["result"] = "pass"
        return report
    finally:
        # 即使断言失败也优先尝试停止PWM，不在调试器暂停状态下结束。
        try:
            state(255, emotion=1, enabled=False, event=65535)
        finally:
            swd.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=6666)
    parser.add_argument("--motion", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "firmware_head/artifacts/hardware_test.json")
    args = parser.parse_args()
    result = run(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
