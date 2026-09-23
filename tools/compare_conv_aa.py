#!/usr/bin/env python3
"""Compare opaque-RGB captures to the existing MSAA reference, not a ground-truth oracle."""
import hashlib
import json
import pathlib
from PIL import Image, ImageChops, ImageDraw, ImageStat


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    folder = root / 'artifacts/conv-aa/final'
    report = {'reference': 'Current 4x MSAA; differences are not absolute quality errors.', 'images': []}
    for moment in [0, 1, 1.04, 2.5]:
        reference = Image.open(folder / f'msaa-{moment}.png').convert('RGB')
        for mode in ['point', 'conv-fast']:
            path = folder / f'{mode}-{moment}.png'
            candidate = Image.open(path).convert('RGB')
            if candidate.size != reference.size:
                raise RuntimeError('Dimensions changed')
            difference = ImageChops.difference(reference, candidate)
            statistics = ImageStat.Stat(difference)
            report['images'].append({'time': moment, 'mode': mode,
                'mean_absolute_255': sum(statistics.mean) / 3,
                'mean_squared_255': sum(value * value for value in statistics.rms) / 3,
                'maximum_255': max(pair[1] for pair in difference.getextrema()),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    repeated = folder / 'conv-repeat-1.png'
    if repeated.exists():
        first = Image.open(folder / 'conv-fast-1.png').convert('RGB')
        second = Image.open(repeated).convert('RGB')
        report['repeat_time_1_pixel_identical'] = ImageChops.difference(first, second).getbbox() is None
    (folder / 'image-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    bounds = (850, 195, 1250, 605)
    figure = Image.new('RGB', (1200, 452), '#11251f')
    text = ImageDraw.Draw(figure)
    for column, mode, label in [(0, 'msaa', '4x MSAA (normal)'), (1, 'point', 'Single sample (control)'), (2, 'conv-fast', 'CONV boundary trial')]:
        image = Image.open(folder / f'{mode}-0.png').convert('RGB')
        figure.paste(image.crop(bounds), (column * 400, 42))
        text.text((column * 400 + 12, 14), label, fill='white')
    figure.save(folder / 'comparison-detail.png')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
