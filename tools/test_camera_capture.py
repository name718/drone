#!/usr/bin/env python3
"""真实OV5640验收：读取PID、完整解码JPEG、帧号递增、MJPEG及并发控制状态请求。"""
import argparse
import hashlib
import io
import json
import time
import urllib.request
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', required=True)
    parser.add_argument('--frames', type=int, default=30)
    parser.add_argument('--width', type=int, default=320)
    parser.add_argument('--height', type=int, default=240)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.frames < 3:
        parser.error('--frames must be >=3')
    folder = args.output.parent
    folder.mkdir(parents=True, exist_ok=True)
    base = f'http://{args.ip}'
    report = {'checks': [], 'frames': [], 'physical_camera': 'OV5640 DVP',
              'transport': 'ESP32-S3 LCD_CAM -> JPEG -> HTTP on existing WiFi',
              'luckfox_image_inference_verified': False,
              'validation_scope': 'JPEG decoding and HTTP transport only; visual quality requires separate inspection',
              'visual_quality_verified': False}

    def status():
        with urllib.request.urlopen(base + '/camera/status', timeout=8) as response:
            return json.load(response)

    def check(name, passed):
        if not passed:
            raise AssertionError(name)
        report['checks'].append(name)
        print('PASS', name, flush=True)

    def decode(data):
        with Image.open(io.BytesIO(data)) as image:
            image.load()  # 真正解码，不是只检查FFD8文件头。
            assert image.format == 'JPEG' and image.size == (args.width, args.height)
            return {'size': list(image.size), 'extrema': image.getextrema()}

    try:
        deadline = time.monotonic() + 25
        while True:
            initial = status()
            if initial['stage'] == 'capturing' and initial['frames'] > 0 and initial.get('recent_frame', True):
                break
            if initial['stage'] in ('failed', 'http_failed') or time.monotonic() > deadline:
                raise RuntimeError(f'Camera not ready: {initial}')
            time.sleep(.3)
        report['initial_status'] = initial
        check('SCCB实读芯片ID为0x5640', initial['sensor_id'] == 0x5640)
        check('后台DVP采集已运行', initial['frames'] > 0)
        started = time.monotonic()
        for i in range(args.frames):
            with urllib.request.urlopen(base + ':81/capture', timeout=8) as response:
                data = response.read(65537)
                sequence = int(response.headers['X-Frame-Sequence'])
            assert len(data) <= 65536
            decoded = decode(data)
            report['frames'].append({'sequence': sequence, 'bytes': len(data),
                                     'sha256': hashlib.sha256(data).hexdigest(), **decoded})
            if i in (0, args.frames - 1) or i % 10 == 0:
                (folder / f'camera_frame_{i:03d}.jpg').write_bytes(data)
            time.sleep(.15)
        elapsed = time.monotonic() - started
        check(f'{args.frames}张JPEG均可完整解码为{args.width}x{args.height}', len(report['frames']) == args.frames)
        check('实际采集帧号持续递增', len({v['sequence'] for v in report['frames']}) >= args.frames // 2)
        stream_frames = []
        with urllib.request.urlopen(base + ':81/stream', timeout=8) as response:
            check('MJPEG响应类型正确', response.headers.get_content_type() == 'multipart/x-mixed-replace')
            for _ in range(3):
                while True:
                    boundary = response.readline(256)
                    if not boundary:
                        raise ConnectionError('MJPEG stream ended before frame boundary')
                    if boundary.strip() == b'--robotframe':
                        break
                    if time.monotonic() - started > 40:
                        raise TimeoutError('MJPEG boundary timeout')
                length = 0
                while True:
                    line = response.readline(256).strip()
                    if not line:
                        break
                    if line.lower().startswith(b'content-length:'):
                        length = int(line.split(b':')[1])
                assert 0 < length <= 65536
                stream_frames.append(decode(response.read(length)))
                # 视频线程正持续发送时，80端口诊断仍应及时响应。
                assert status()['stage'] == 'capturing'
        check('3帧MJPEG解码且并发控制HTTP正常', len(stream_frames) == 3)
        final = status()
        report['final_status'] = final
        report['capture_fps_estimate'] = round((final['frames'] - initial['frames']) /
                                              (time.monotonic() - started), 2)
        report['snapshot_test_seconds'] = round(elapsed, 3)
        check('采集期间无新增失败帧', final['failures'] == initial['failures'])
        report['result'] = 'pass'
    except Exception as error:
        report.update(result='fail', error=str(error))
        raise
    finally:
        app = Path(__file__).resolve().parents[1] / 'firmware_brain/build/firmware_brain.bin'
        report['esp_firmware_sha256'] = hashlib.sha256(app.read_bytes()).hexdigest()
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
