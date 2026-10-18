#!/usr/bin/env python3
"""电脑→ESP网页协议→UART2→Luckfox桥→C8→原路返回ESP的实物测试。

需要舵机动力电源断开；指令角度变化仍会产生PWM，但不验证机械实际位置。
用SWD只读C8状态核实表情，不用SWD注入命令。OpenOCD先启动且不重置C8。
结束恢复NORMAL和29°/0°，关闭自主动作，保持桥服务运行。
"""
import argparse
import hashlib
import json
from pathlib import Path
import selectors
import subprocess
import time

from head_swd_test import FIELDS, ROOT, Swd, symbols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--adb", help="提供后额外验证单向断线；需先部署status_only_probe.py")
    parser.add_argument("--serial")
    args = parser.parse_args()
    if args.adb and not args.serial:
        parser.error("--adb需要同时提供--serial")
    report = {"transport": "WebSocket -> real ESP32-S3 -> UART2 -> real Luckfox UART3/4 -> real C8 -> ESP telemetry",
              "mechanical_servo_motion_verified": False, "checks": []}
    swd = Swd(6666)
    address = symbols(ROOT / "firmware_head/build/firmware_head.elf")["g_head_diag"]
    child = subprocess.Popen(["node", str(ROOT / "tools/head_websocket_probe.mjs"), f"ws://{args.ip}/ws"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             bufsize=0)
    buffered = bytearray()
    poll = selectors.DefaultSelector()
    poll.register(child.stdout, selectors.EVENT_READ)

    def send(data):
        child.stdin.write(json.dumps(data, separators=(",", ":")).encode() + b"\n")

    def telemetry(predicate=lambda _s: True, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while b"\n" in buffered:
                line, _, remaining = buffered.partition(b"\n")
                buffered[:] = remaining
                value = json.loads(line)
                if value.get("type") == "telemetry" and predicate(value):
                    return value
            if child.poll() is not None:
                raise RuntimeError("WebSocket客户端提前退出")
            for key, _ in poll.select(0.2):
                data = key.fileobj.read(8192)
                if not data:
                    raise ConnectionError("WebSocket遥测流关闭")
                buffered.extend(data)
        raise TimeoutError("等待ESP遥测超时")

    def diag():
        # Cortex-M3可经调试总线读取运行中的SRAM。不能halt：ESP仍在发UART，
        # 暂停CPU会阻塞接收中断，反而人为制造串口overrun。字段是近似快照，
        # 不把多个字段读取的时刻当作原子时刻。
        return dict(zip(FIELDS, swd.read(address, 32, len(FIELDS))))

    def wait_diag(predicate):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            value = diag()
            if predicate(value):
                return value
            time.sleep(0.05)
        raise TimeoutError("C8没有达到期望状态")

    def check(name, value=True):
        if not value:
            raise AssertionError(name)
        report["checks"].append(name)
        print("PASS", name, flush=True)

    try:
        first = telemetry(lambda s: s.get("head_online") is True)
        check("ESP网页遥测确认C8在线", first["head_age_ms"] < 1000)
        baseline = diag()
        report["initial_c8"] = baseline
        send({"type": "autonomous", "enable": False})
        telemetry(lambda s: s["auto_mode"] is False)
        check("关闭自主动作以便验证明确指令")
        send({"type": "emotion", "emotion": "SLEEPY"})
        sleepy = wait_diag(lambda s: s["emotion"] == 4)
        check("ESP发SLEEPY后C8实际切换表情", sleepy["uart_valid_frames"] > baseline["uart_valid_frames"])
        send({"type": "emotion", "emotion": "SURPRISED"})
        wait_diag(lambda s: s["emotion"] == 3 and s["target_tilt_cdeg"] == 1600)
        check("ESP发SURPRISED后C8表情和目标角度均更新")
        send({"type": "gimbal_angle", "pan": 30, "tilt": 1})
        wait_diag(lambda s: s["target_pan_cdeg"] == 3000 and s["target_tilt_cdeg"] == 100)
        check("ESP角度命令经真实三板到达C8")
        reached = telemetry(lambda s: s["head_online"] and abs(s["g_pan"] - 30) <= .1 and abs(s["g_tilt"] - 1) <= .1)
        check("C8计算角度经串口返回并显示在ESP遥测")
        check("头部状态保持新鲜", reached["head_age_ms"] < 500)
        send({"type": "emotion", "emotion": "NORMAL"})
        final = wait_diag(lambda s: s["emotion"] == 1 and s["pan_cdeg"] == 2900 and s["tilt_cdeg"] == 0)
        report["final_telemetry"] = telemetry(lambda s: s["head_online"] and abs(s["g_pan"] - 29) <= .1 and abs(s["g_tilt"]) <= .1)
        check("结束恢复NORMAL及29度/0度")
        if args.adb:
            adb = [args.adb, "-s", args.serial]
            def service(action):
                subprocess.run(adb + ["shell", f"/etc/init.d/S99zheadbridge {action}"],
                               check=True, capture_output=True, timeout=12)
            service("stop")
            one_way = None
            try:
                one_way = subprocess.Popen(adb + ["shell", "cd /userdata/head_bridge && PYTHONDONTWRITEBYTECODE=1 python3 status_only_probe.py"],
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                state = telemetry(lambda s: not s["head_online"] and s["head_age_ms"] < 500, timeout=8)
                wait_diag(lambda s: s["link_online"] == 0 and s["servo_enabled"] == 0)
                check("单向状态仍新鲜但控制断开时正确判离线", state["head_age_ms"] < 500)
            finally:
                try:
                    if one_way is not None:
                        one_way.communicate(timeout=12)
                finally:
                    service("start")
            report["final_telemetry"] = telemetry(lambda s: s["head_online"], timeout=8)
            final = wait_diag(lambda s: s["link_online"] == 1 and s["emotion"] == 1)
            check("恢复正式桥后双向控制自动恢复")
        check("通信测试无新增UART硬件错误", final["uart_errors"] == baseline["uart_errors"])
        check("通信测试无新增CRC错误", final["crc_errors"] == baseline["crc_errors"])
        check("最终无SPI错误和HardFault", final["spi_errors"] == 0 and final["last_fault"] == 0)
        report.update(result="pass", final_c8=final)
    except Exception as error:
        report.update(result="fail", error=str(error))
    finally:
        if child.poll() is None:
            try:
                send({"type": "autonomous", "enable": False})
                send({"type": "emotion", "emotion": "NORMAL"})
                time.sleep(0.3)
            except (BrokenPipeError, OSError):
                pass
        child.terminate()
        child.communicate(timeout=5)
        poll.close()
        swd.close()
    report["c8_firmware_sha256"] = hashlib.sha256((ROOT / "firmware_head/build/firmware_head.bin").read_bytes()).hexdigest()
    report["esp_firmware_sha256"] = hashlib.sha256((ROOT / "firmware_brain/build/firmware_brain.bin").read_bytes()).hexdigest()
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
