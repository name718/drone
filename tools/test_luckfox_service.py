#!/usr/bin/env python3
"""在电脑上驱动ADB，验证实机服务生命周期，避免小板同时运行多份Python测试。

会杀掉本服务自己的桥子进程以测试恢复；--reboot还会重启Luckfox。
不发送STATE或使能舵机。运行前保持UART3没有ESP持续控制命令。
"""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--reboot", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    base = [args.adb, "-s", args.serial]
    report = {"transport": "ADB lifecycle control; no servo commands", "checks": []}

    def shell(command, required=True):
        result = subprocess.run(base + ["shell", command], capture_output=True, text=True, timeout=10)
        if required and result.returncode:
            raise RuntimeError(result.stderr + result.stdout)
        return result.stdout.strip()

    def check(name, condition):
        if not condition:
            raise AssertionError(name)
        report["checks"].append(name)
        print("PASS", name, flush=True)

    def wait_for(function, timeout=40):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = function()
            if value:
                return value
            time.sleep(0.5)
        raise TimeoutError("实机服务等待超时")

    def pid(which):
        value = shell(f"cat /run/head-bridge/{which}.pid", required=False)
        return int(value) if value.isdigit() else 0

    try:
        check("目标板型为Mini", shell("cat /proc/device-tree/model").rstrip("\0") == "Luckfox Pico Mini")
        shell("/etc/init.d/S99zheadbridge start")
        first = wait_for(lambda: pid("supervisor"))
        child = wait_for(lambda: pid("child"))
        shell("/etc/init.d/S99zheadbridge start")
        check("重复start不新增服务", pid("supervisor") == first and pid("child") == child)
        shell(f"kill -KILL {child}")  # 只操作上面从本服务pidfile读到的子进程。
        replacement = wait_for(lambda: (new if (new := pid("child")) and new != child else 0))
        check("子进程异常退出后重新启动", replacement != child and pid("supervisor") == first)
        shell("/etc/init.d/S99zheadbridge stop")
        check("stop清理本服务PID文件", pid("supervisor") == 0 and pid("child") == 0)
        shell("/etc/init.d/S99zheadbridge start; sync")
        report["boot_before"] = shell("cat /proc/sys/kernel/random/boot_id")
        if args.reboot:
            subprocess.run(base + ["reboot"], check=True, timeout=10)
            def new_boot():
                value = shell("cat /proc/sys/kernel/random/boot_id", required=False)
                return value if value and value != report["boot_before"] else ""
            report["boot_after"] = wait_for(new_boot)
            wait_for(lambda: pid("supervisor"))
            wait_for(lambda: pid("child"))
            check("真实重启后服务自动启动", True)
        check("两路UART节点存在", shell("test -c /dev/ttyS3 && test -c /dev/ttyS4 && echo OK") == "OK")
        # 等桥完成模块导入和端口打开，确认最终活着的子进程实际拥有两个UART。
        def owns_ports():
            current = pid("child")
            if not current:
                return False
            listing = shell(f"ls -l /proc/{current}/fd", required=False)
            return "/dev/ttyS3" in listing and "/dev/ttyS4" in listing
        wait_for(owns_ports)
        check("最终桥进程已打开UART3和UART4", True)
        report["supervisor_pid"], report["bridge_pid"] = pid("supervisor"), pid("child")
        report["memory"] = shell("head -n 5 /proc/meminfo")
        report["result"] = "pass"
    except Exception as error:
        report.update(result="fail", error=str(error))
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
