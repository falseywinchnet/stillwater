#!/usr/bin/env python3
"""Matched camera-registry measurements on the native target GPU."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess

from measure import measure


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / 'artifacts/camera-cache/performance'
    output.mkdir(parents=True, exist_ok=True)
    executable = root / 'build/Stillwater.app/Contents/MacOS/Stillwater'
    report = {
        'machine': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
        'os': platform.mac_ver()[0],
        'note': '4x MSAA, native desktop, muted, 24 fps. Four seconds warmup and 12 seconds warm CPU sampling. GPU duration includes acquisition/classification and final command drain. Registration uses a rare CPU count readback; animation does not. No power inference.',
        'sha256': {}, 'cases': {},
    }
    for path in [executable, root / 'assets/aquarium.metal', root / 'assets/camera_registry.metal', root / 'src/metal_renderer.cpp']:
        report['sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for label, extra in [('dense-before', ['--dense-camera']), ('camera-first', ['--compact-camera']),
                         ('camera-second', ['--compact-camera']), ('dense-after', ['--dense-camera'])]:
        result = measure(root, label, ['--desktop', '--muted', '--msaa', '4', *extra], 12, executable, output)
        report['cases'][label] = result
        (output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')
        print(label, result['mean_gpu_ms_per_timed_frame'], result['gpu_allocated_bytes'], flush=True)


if __name__ == '__main__':
    main()
