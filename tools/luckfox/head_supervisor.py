#!/usr/bin/env python3
"""Buildroot上的小型进程看护：单实例、退出后重启、收到TERM后停止子进程。

不生成舵机命令；桥退出期间由C8的一秒心跳超时关闭PWM。
运行状态放/run（内存），日志送syslog，避免循环写入板载Flash。
"""
from __future__ import annotations

import argparse
import fcntl
import logging
import logging.handlers
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading


def supervise(run_dir: Path, esp: str, head: str) -> int:
    run_dir.mkdir(parents=True, exist_ok=True)
    # flock由内核随进程退出释放，意外掉电后不会留下永久的“假锁”。
    with (run_dir / "lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            logging.error("head bridge supervisor already running")
            return 1
        pidfile = run_dir / "supervisor.pid"
        childfile = run_dir / "child.pid"
        pidfile.write_text(str(os.getpid()) + "\n")
        stopped = threading.Event()
        child = None

        def stop(_signum, _frame):
            stopped.set()

        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        try:
            while not stopped.is_set():
                child = subprocess.Popen(
                    [sys.executable, str(Path(__file__).with_name("head_bridge.py")),
                     "--esp", esp, "--head", head, "--syslog"],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
                )
                childfile.write_text(str(child.pid) + "\n")
                logging.info("head bridge child started pid=%d", child.pid)
                while child.poll() is None and not stopped.wait(0.1):
                    pass
                if stopped.is_set():
                    break
                logging.warning("head bridge exited code=%s; retry in 3s", child.returncode)
                childfile.unlink(missing_ok=True)
                stopped.wait(3)
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            childfile.unlink(missing_ok=True)
            pidfile.unlink(missing_ok=True)
            logging.info("head bridge supervisor stopped")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, default=Path("/run/head-bridge"))
    parser.add_argument("--esp", default="/dev/ttyS3")
    parser.add_argument("--head", default="/dev/ttyS4")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="head-supervisor: %(levelname)s %(message)s")
    if Path("/dev/log").exists():
        handler = logging.handlers.SysLogHandler(address="/dev/log")
        handler.setFormatter(logging.Formatter("head-supervisor: %(levelname)s %(message)s"))
        logging.getLogger().handlers = [handler]
    return supervise(args.run_dir, args.esp, args.head)


if __name__ == "__main__":
    raise SystemExit(main())
