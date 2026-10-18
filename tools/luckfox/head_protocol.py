"""与 protocol/head_protocol.h 对应的协议编码器；仅使用 Python 标准库。"""
from __future__ import annotations
import binascii
import dataclasses
import struct
import time

VERSION = 1
MAX_PAYLOAD = 64
PING, STATE, CAMERA, STATUS, ACK = 1, 0x10, 0x11, 0x80, 0x81
STATE_FORMAT = "<BBBBHHH"
STATUS_FORMAT = "<IIIIIHHHHHBBBBH"


def encode(kind: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 64 bytes")
    body = bytes((VERSION, kind, sequence & 0xFF, len(payload))) + payload
    return b"\xa5\x5a" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))


@dataclasses.dataclass(frozen=True)
class Frame:
    kind: int
    sequence: int
    payload: bytes
    wire: bytes


class Parser:
    """有限缓存、CRC校验、逐字节重新同步；串口输入不是一次read一整帧。"""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.last_byte = 0.0
        self.crc_errors = 0

    def feed(self, data: bytes, now: float | None = None) -> list[Frame]:
        now = time.monotonic() if now is None else now
        if self.buffer and now - self.last_byte > 0.1:
            self.buffer.clear()
        self.last_byte = now
        frames = []
        # 逐字节推进，内存上界72字节，即使输入是任意噪声也不会无限增长。
        for value in data:
            self.buffer.append(value)
            while len(self.buffer) >= 2:
                if self.buffer[:2] != b"\xa5\x5a":
                    del self.buffer[0]
                    continue
                if len(self.buffer) < 6:
                    break
                if self.buffer[2] != VERSION or self.buffer[5] > MAX_PAYLOAD:
                    del self.buffer[0]
                    continue
                size = self.buffer[5] + 8
                if len(self.buffer) < size:
                    break
                wire = bytes(self.buffer[:size])
                expected = struct.unpack("<H", wire[-2:])[0]
                if expected != binascii.crc_hqx(wire[2:-2], 0xFFFF):
                    self.crc_errors += 1
                    del self.buffer[0]
                    continue
                frames.append(Frame(wire[3], wire[4], wire[6:-2], wire))
                del self.buffer[:size]
        return frames


def state_payload(emotion=1, energy=0, gesture=0, enabled=False, pan=2900, tilt=0, event=0):
    """角度单位0.01°；必须明确enabled=True，默认不会驱动舵机。"""
    return struct.pack(STATE_FORMAT, emotion, energy, gesture, int(enabled), pan, tilt, event)
