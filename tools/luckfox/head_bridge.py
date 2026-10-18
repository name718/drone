#!/usr/bin/env python3
"""在Luckfox转发头部控制帧，不执行推理，也不把JPEG错误地送给C8T6。

python3 head_bridge.py --esp /dev/ttyS3 --head /dev/ttyS4
先在luckfox-config中启用这两组串口，关闭占用它们的getty/其他程序。
"""
from __future__ import annotations
import argparse
import logging
import logging.handlers
import os
import selectors
import signal
import termios
from head_protocol import ACK, CAMERA, PING, STATE, STATUS, Parser


def open_port(path: str, baud: int) -> int:
    speed = getattr(termios, f"B{baud}", None)
    if speed is None:
        raise ValueError(f"系统不支持波特率 {baud}")
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        a = termios.tcgetattr(fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CLOCAL | termios.CREAD | termios.CS8
        a[4] = a[5] = speed
        a[6][termios.VMIN] = a[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, a)
        termios.tcflush(fd, termios.TCIOFLUSH)
        return fd
    except BaseException:
        os.close(fd)
        raise


def bridge(esp_path: str, head_path: str, baud: int = 115200) -> None:
    if os.path.realpath(esp_path) == os.path.realpath(head_path):
        raise ValueError("两端不能使用同一个串口")
    ports = []
    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        ports.append(open_port(esp_path, baud))
        ports.append(open_port(head_path, baud))
        pending = [bytearray(), bytearray()]
        parsers = [Parser(), Parser()]
        allowed = [{PING, STATE, CAMERA}, {STATUS, ACK}]
        with selectors.DefaultSelector() as poll:
            for index, fd in enumerate(ports):
                poll.register(fd, selectors.EVENT_READ, index)
            logging.info("头部桥已启动: %s <-> %s，%d 8N1", esp_path, head_path, baud)
            while running:
                for key, events in poll.select(0.1):
                    index = key.data
                    if events & selectors.EVENT_READ:
                        try:
                            data = os.read(key.fd, 4096)
                        except BlockingIOError:
                            data = None
                        if data == b"":
                            raise ConnectionError("UART端口已关闭")
                        for frame in parsers[index].feed(data or b""):
                            if frame.kind not in allowed[index]:
                                continue
                            destination = 1 - index
                            if len(pending[destination]) + len(frame.wire) > 4096:
                                # 失去消费能力时退出，由进程管理器重启；C8会在1秒后停止PWM。
                                raise BufferError("UART发送队列已满，链路消费者可能失效")
                            pending[destination].extend(frame.wire)
                    if events & selectors.EVENT_WRITE and pending[index]:
                        try:
                            count = os.write(key.fd, pending[index])
                        except BlockingIOError:
                            count = 0
                        del pending[index][:count]
                for index, fd in enumerate(ports):
                    events = selectors.EVENT_READ | (selectors.EVENT_WRITE if pending[index] else 0)
                    poll.modify(fd, events, index)
    finally:
        for fd in ports:
            os.close(fd)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp", default="/dev/ttyS3")
    parser.add_argument("--head", default="/dev/ttyS4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--syslog", action="store_true", help="服务模式下把日志送syslog，避免持续写Flash")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if args.syslog and os.path.exists("/dev/log"):
        handler = logging.handlers.SysLogHandler(address="/dev/log")
        handler.setFormatter(logging.Formatter("head-bridge: %(levelname)s %(message)s"))
        logging.getLogger().handlers = [handler]
    bridge(args.esp, args.head, args.baud)


if __name__ == "__main__":
    main()
