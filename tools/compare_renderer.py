#!/usr/bin/env python3
"""Compare a shader change against prior native verification captures.

Uses the existing visual error bounds for both rendered paths. A storage-only
change must instead use compare_storage.py, which requires identical full redraw.
"""
import argparse
import json
from pathlib import Path
from compare_storage import image_error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('baseline', type=Path)
    parser.add_argument('current', type=Path)
    args = parser.parse_args()
    before = json.loads((args.baseline / 'native-verification.json').read_text())
    after = json.loads((args.current / 'native-verification.json').read_text())
    if before['failures'] or after['failures'] or before['comparisons'] != after['comparisons']:
        raise RuntimeError('Native checks failed or case counts changed')
    report = {'passed': True, 'images': {}, 'exact_probes': {}}
    probes = sorted(args.baseline.glob('*-probes.json'))
    if len(probes) != before['comparisons']:
        raise RuntimeError('Missing reference probes')
    for path in probes:
        name = path.name.removesuffix('-probes.json')
        for kind in ['full', 'retained']:
            filename = f'{name}-{kind}.png'
            error = image_error(args.baseline / filename, args.current / filename)
            error['passed'] = error['maximum_255'] <= 8 and error['mean_absolute_255'] < .01
            report['images'][filename] = error
            report['passed'] = report['passed'] and error['passed']
        exact = json.loads(path.read_text()) == json.loads((args.current / path.name).read_text())
        report['exact_probes'][name] = exact
        report['passed'] = report['passed'] and exact
    (args.current / 'renderer-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
