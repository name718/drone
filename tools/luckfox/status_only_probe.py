#!/usr/bin/env python3
"""仅供电脑驱动的5秒故障注入：丢弃ESP命令，仍将C8状态回传给ESP。

运行前停止正式桥；结束后重新启动。用于验证“能收到状态”不等于
双向控制在线。程序不产生STATE命令，5秒后自行关闭端口并退出。
"""
import os
import select
import time

from head_bridge import open_port
from head_protocol import STATUS, Parser


def main():
    ports = []
    try:
        ports.append(open_port("/dev/ttyS3", 115200))
        ports.append(open_port("/dev/ttyS4", 115200))
        esp, head = ports
        parser = Parser()
        pending = bytearray()
        end = time.monotonic() + 5
        while time.monotonic() < end:
            readable, writable, _ = select.select(ports, [esp] if pending else [], [], .05)
            for fd in readable:
                data = os.read(fd, 4096)
                if not data:
                    raise ConnectionError("UART已关闭")
                if fd == head:
                    for frame in parser.feed(data):
                        if frame.kind == STATUS:
                            pending.extend(frame.wire)
                            if len(pending) > 4096:
                                raise BufferError("回传队列已满")
                # ESP发来的控制数据在本测试中明确丢弃。
            if esp in writable:
                try:
                    count = os.write(esp, pending)
                    del pending[:count]
                except BlockingIOError:
                    pass
    finally:
        for fd in ports:
            os.close(fd)


if __name__ == "__main__":
    main()
