#!/usr/bin/env python3
"""相机内部彩条验收：按色块顺序检查RGB主色，不以JPEG可解码代替颜色正确。"""
import argparse
import io
import json
import time
import urllib.request
from pathlib import Path
from PIL import Image, ImageStat


def analyze(data):
    with Image.open(io.BytesIO(data)) as image:
        image.load()
        image = image.convert('RGB')
        bars = []
        for i in range(8):
            center = image.width * (i + .5) / 8
            patch = image.crop((int(center - image.width / 64), image.height // 3,
                                int(center + image.width / 64), 2 * image.height // 3))
            bars.append(ImageStat.Stat(patch).median)
    white, yellow, cyan, green, magenta, red, blue, black = bars
    checks = {
        'white': min(white) > 180 and max(white) - min(white) < 40,
        'yellow': min(yellow[:2]) > yellow[2] + 40,
        'cyan': min(cyan[1:]) > cyan[0] + 40,
        'green': green[1] > max(green[0], green[2]) + 40,
        'magenta': min(magenta[0], magenta[2]) > magenta[1] + 40,
        'red': red[0] > max(red[1], red[2]) + 40,
        'blue': blue[2] > max(blue[0], blue[1]) + 40,
        'black': max(black) < 50,
    }
    return {'bars_rgb': bars, 'checks': checks, 'result': 'pass' if all(checks.values()) else 'fail'}


def mjpeg_frames(stream, count):
    """逐个读取连续帧，避免单张快照取样与隔帧异常发生混叠。"""
    for _ in range(count):
        while True:
            line = stream.readline(256)
            if not line: raise ConnectionError('MJPEG ended')
            if line.strip() == b'--robotframe': break
        length = 0
        while True:
            line = stream.readline(256).strip()
            if not line: break
            if line.lower().startswith(b'content-length:'):
                length = int(line.split(b':')[1])
        if not 0 < length <= 65536: raise ValueError('invalid JPEG length')
        data = stream.read(length)
        if len(data) != length: raise ConnectionError('truncated JPEG')
        yield data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', required=True)
    parser.add_argument('--modes', default='5')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=20)
    args = parser.parse_args()
    if args.frames < 2: parser.error('--frames must be >=2')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    base = f'http://{args.ip}'
    entries = []

    def get(path):
        with urllib.request.urlopen(base + path, timeout=6) as response:
            return response.read()

    def mode(value):
        request = urllib.request.Request(base + f'/camera/color?mode={value}', data=b'', method='POST')
        with urllib.request.urlopen(request, timeout=6) as response:
            assert response.read() == b'queued'

    initial = json.loads(get('/camera/status'))
    try:
        for value in map(int, args.modes.split(',')):
            entry = {'mode': value}
            entries.append(entry)
            try:
                mode(value)
                deadline = time.monotonic() + 10
                while True:
                    state = json.loads(get('/camera/status'))
                    if state['color_mode'] == value and state['recent_frame']:
                        break
                    if time.monotonic() >= deadline:
                        raise TimeoutError(state)
                    time.sleep(.2)
                time.sleep(1)
                samples = []
                with urllib.request.urlopen(base + ':81/stream', timeout=8) as stream:
                    for frame, data in enumerate(mjpeg_frames(stream, args.frames)):
                        sample = {'frame': frame, **analyze(data)}
                        samples.append(sample)
                        if frame == 0 or (sample['result'] != 'pass' and not any(x['result'] != 'pass' for x in samples[:-1])):
                            (args.output.parent / f'camera_color_bar_mode{value}_frame{frame}.jpg').write_bytes(data)
                entry['samples'] = samples
                entry['result'] = 'pass' if all(x['result'] == 'pass' for x in samples) else 'fail'
                entry['failed_frames'] = [x['frame'] for x in samples if x['result'] != 'pass']
                entry['registers'] = json.loads(get('/camera/registers'))
                entry['status'] = json.loads(get('/camera/status'))
            except Exception as error:
                entry.update(result='error', error=str(error))
            print(json.dumps({k:v for k,v in entry.items() if k != 'samples'}), flush=True)
    finally:
        mode(initial['color_mode'])
        args.output.write_text(json.dumps({'entries': entries,
                                          'scope': 'sensor-generated color bars; real-object accuracy requires separate check'},
                                         ensure_ascii=False, indent=2) + '\n')
    if not entries or any(entry.get('result') != 'pass' for entry in entries):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
