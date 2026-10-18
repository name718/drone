#!/usr/bin/env python3
"""固定场景连续MJPEG验色：检测正常/偏紫隔帧交替，白平衡应保持同一模式。"""
import argparse
import hashlib
import io
import json
import time
import urllib.request
from pathlib import Path
from PIL import Image, ImageStat
from test_camera_color import mjpeg_frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', required=True)
    parser.add_argument('--frames', type=int, default=60)
    parser.add_argument('--neutral-roi', action='store_true', help='ROI为固定白/灰参照时，另检查持续偏紫')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.frames < 3: parser.error('--frames must be >=3')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    base = f'http://{args.ip}'
    report = {'scope': 'Fixed scene needed; temporal color stability, not calibrated colorimetry', 'frames': []}
    started = time.monotonic()
    try:
        with urllib.request.urlopen(base + ':81/stream', timeout=10) as stream:
            for index, data in enumerate(mjpeg_frames(stream, args.frames)):
                image = Image.open(io.BytesIO(data))
                image.load()
                image = image.convert('RGB')
                roi = image.crop((150, 30, min(500, image.width), min(180, image.height)))
                r, g, b = ImageStat.Stat(roi).mean
                score = (r + b - 2 * g) / (r + g + b + 1)
                row = {'frame': index, 'rgb_mean': [r, g, b], 'chroma_score': score,
                       'sha256': hashlib.sha256(data).hexdigest()}
                if report['frames']:
                    row['chroma_step'] = abs(score - report['frames'][-1]['chroma_score'])
                report['frames'].append(row)
                if index % 10 == 0 or row.get('chroma_step', 0) > .15:
                    (args.output.parent / f'camera_stability_{index:03d}.jpg').write_bytes(data)
        steps = [row.get('chroma_step', 0) for row in report['frames']]
        report['large_color_transitions'] = sum(step > .15 for step in steps)
        report['max_color_step'] = max(steps)
        report['mean_chroma_score'] = sum(row['chroma_score'] for row in report['frames']) / len(report['frames'])
        report['neutral_roi_required'] = args.neutral_roi
        report['seconds'] = time.monotonic() - started
        report['observed_fps'] = args.frames / report['seconds']
        with urllib.request.urlopen(base + '/camera/status', timeout=5) as response:
            report['final_status'] = json.load(response)
        report['result'] = 'pass' if report['large_color_transitions'] <= max(1, args.frames // 20) and (not args.neutral_roi or abs(report['mean_chroma_score']) < .25) else 'fail'
    except Exception as error:
        report.update(result='error', error=str(error))
        raise
    finally:
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'frames'}), flush=True)
    if report['result'] != 'pass': raise SystemExit(1)


if __name__ == '__main__':
    main()
