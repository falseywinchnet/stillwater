#!/usr/bin/env python3
"""Measure the experimental geometry-coverage adapter against unchanged 4x MSAA."""
import hashlib
import json
import pathlib
import platform
import subprocess

from measure import measure


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    output = root / 'artifacts/conv-aa/final'
    output.mkdir(parents=True, exist_ok=True)
    executable = root / 'build/Stillwater.app/Contents/MacOS/Stillwater'
    report = {
        'machine': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
        'os': platform.mac_ver()[0],
        'note': 'Native desktop, 1408x881 expected, 24 fps, muted. Four seconds warmup, 12-second CPU interval. GPU command timings include startup and drain every submitted frame; not a power measurement. CONV trial is a single-sample depth-tested boundary overlay, not exact visibility composition. Point is an unantialiased storage/control baseline.',
        'sha256': {}, 'cases': {}, 'captures': [],
    }
    for path in [executable, root / 'assets/aquarium.metal', root / 'assets/conv_coverage.metal', root / 'assets/conv_geometry.metal', root / 'src/metal_renderer.cpp']:
        report['sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for label, mode in [('msaa-before', 'msaa'), ('conv-first', 'conv-fast'), ('point', 'point'), ('conv-second', 'conv-fast'), ('msaa-after', 'msaa')]:
        result = measure(root, label, ['--desktop', '--muted', '--aa', mode], 12, executable, output)
        report['cases'][label] = result
        (output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')
        print(label, json.dumps(result), flush=True)
    for moment in [0, 1, 1.04, 2.5]:
        for mode in ['msaa', 'point', 'conv-fast']:
            label = f'{mode}-{moment}'
            capture = output / (label + '.png')
            with (output / (label + '.log')).open('w') as log:
                subprocess.run([str(executable), '--desktop', '--muted', '--paused', '--aa', mode, '--capture-time', str(moment), '--capture', str(capture), '--metrics', str(output / (label + '.json')), '--quit-after', '0.2'], stdout=log, stderr=subprocess.STDOUT, check=True)
            if not capture.is_file():
                raise RuntimeError('Missing capture: ' + label)
            report['captures'].append({'mode': mode, 'time': moment, 'path': str(capture.relative_to(root)), 'sha256': hashlib.sha256(capture.read_bytes()).hexdigest()})
    (output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
