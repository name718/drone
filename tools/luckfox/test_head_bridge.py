"""通过两个PTY测试真实桥进程，验证两个方向、分片、噪声及错误CRC。"""
import os
import pty
import select
import struct
import subprocess
import sys
import time
import signal
import tempfile
import unittest
from pathlib import Path
from head_protocol import ACK, CAMERA, PING, STATE, STATUS, Parser, encode, state_payload


def read_frame(fd, timeout=2):
    parser = Parser()
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if select.select([fd], [], [], 0.05)[0]:
            frames = parser.feed(os.read(fd, 4096))
            if frames:
                return frames[0]
    raise TimeoutError("没有收到完整协议帧")


class ProtocolTest(unittest.TestCase):
    def test_supervisor_restart_and_single_instance(self):
        # 用真实子进程验证守护逻辑；仅操作本测试创建的PTY和PID。
        ports = [pty.openpty(), pty.openpty()]
        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary)
            command = [sys.executable, str(Path(__file__).with_name("head_supervisor.py")),
                       "--run-dir", temporary, "--esp", os.ttyname(ports[0][1]),
                       "--head", os.ttyname(ports[1][1])]
            proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            def child_pid(previous=0):
                end = time.monotonic() + 10
                while time.monotonic() < end:
                    self.assertIsNone(proc.poll(), "supervisor提前退出")
                    try:
                        current = int((run / "child.pid").read_text())
                        if current != previous:
                            return current
                    except (FileNotFoundError, ValueError):
                        pass
                    time.sleep(0.05)
                self.fail("supervisor没有启动/重启子进程")
            try:
                first = child_pid()
                duplicate = subprocess.run(command, capture_output=True, timeout=15)
                self.assertEqual(duplicate.returncode, 1)
                os.kill(first, signal.SIGKILL)
                self.assertNotEqual(child_pid(first), first)
            finally:
                proc.terminate()
                proc.wait(timeout=8)
                for pair in ports:
                    for fd in pair:
                        os.close(fd)
            self.assertFalse((run / "supervisor.pid").exists())
            self.assertFalse((run / "child.pid").exists())

    def test_golden(self):
        # 与C++标准测试向量共用CRC算法规范，不调用C实现。
        self.assertEqual(encode(PING, 7).hex(), "a55a01010700d35c")
        p = Parser()
        valid = encode(STATE, 8, state_payload(emotion=2))
        broken = bytearray(valid)
        broken[-1] ^= 1
        self.assertEqual(p.feed(broken), [])
        frames = p.feed(valid[:4]) + p.feed(valid[4:])
        self.assertEqual(frames[0].wire, valid)
        self.assertEqual(p.crc_errors, 1)

    def test_real_bridge(self):
        esp, esp_slave = pty.openpty()
        head, head_slave = pty.openpty()
        proc = subprocess.Popen(
            [sys.executable, str(Path(__file__).with_name("head_bridge.py")),
             "--esp", os.ttyname(esp_slave), "--head", os.ttyname(head_slave)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        try:
            # 等待桥完成termios设置；启动日志代表两个端口都成功打开。
            self.assertTrue(select.select([proc.stderr], [], [], 3)[0])
            self.assertIn("已启动", proc.stderr.readline().decode())
            wire = encode(STATE, 3, state_payload())
            os.write(esp, b"noise" + wire[:3])
            time.sleep(0.01)
            os.write(esp, wire[3:])
            self.assertEqual(read_frame(head).wire, wire)
            answer = encode(ACK, 3, bytes((STATE, 0)))
            os.write(head, answer)
            self.assertEqual(read_frame(esp).wire, answer)
            bad = bytearray(wire); bad[-1] ^= 1
            os.write(esp, bad + encode(CAMERA, 4, b"\x00"))
            self.assertEqual(read_frame(head).kind, CAMERA)
            # 图像/未知类型不会被误发给C8。
            os.write(esp, encode(0x40, 5, b"jpeg") + encode(PING, 6))
            self.assertEqual(read_frame(head).kind, PING)
        finally:
            proc.terminate()
            proc.communicate(timeout=3)
            for fd in (esp, esp_slave, head, head_slave):
                os.close(fd)


if __name__ == "__main__":
    unittest.main()
