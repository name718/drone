#!/usr/bin/env python3
"""只在相机采集任务支持的预设档位间调试；以实际JPEG解码选择可工作的档位。"""
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
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = f'http://{args.ip}'
    report = {'profiles': [], 'result': 'fail'}

    def status():
        with urllib.request.urlopen(base + '/camera/status', timeout=3) as r:
            return json.load(r)

    try:
        for profile in (1, 0, 3, 2, 4, 5, 6):
            req = urllib.request.Request(base + f'/camera/profile?id={profile}', data=b'', method='POST')
            with urllib.request.urlopen(req, timeout=5) as r:
                assert r.read() == b'queued'
            entry = {'profile': profile, 'sequences': [], 'result': 'fail'}
            report['profiles'].append(entry)
            deadline = time.monotonic() + 18
            while time.monotonic() < deadline and len(entry['sequences']) < 3:
                try:
                    state = status()
                    entry['status'] = state
                    if state['profile'] != profile:
                        time.sleep(.3)
                        continue
                    with urllib.request.urlopen(base + ':81/capture', timeout=3) as r:
                        data, sequence = r.read(65537), int(r.headers['X-Frame-Sequence'])
                    with Image.open(io.BytesIO(data)) as image:
                        image.load()
                        assert image.format == 'JPEG' and image.size == (320, 240)
                    if sequence not in entry['sequences']:
                        entry['sequences'].append(sequence)
                    entry['last_frame_sha256'] = hashlib.sha256(data).hexdigest()
                    (args.output.parent / 'camera_profile_frame.jpg').write_bytes(data)
                except (OSError, ValueError, AssertionError) as error:
                    entry['last_error'] = str(error)
                time.sleep(.25)
            if len(entry['sequences']) == 3:
                entry['result'] = report['result'] = 'pass'
                report['selected_profile'] = profile
                print('PASS', json.dumps(entry), flush=True)
                break
            print('FAIL', json.dumps(entry), flush=True)
        if report['result'] != 'pass':
            raise RuntimeError('No tested timing profile produced three decodable JPEG frames')
    finally:
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')


if __name__ == '__main__':
    main()
