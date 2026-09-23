#!/usr/bin/env python3
"""Compare foliage specialization and optional per-record fixed shading."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess
from measure import measure


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / 'artifacts/render-costs'
    executable = root / 'build/Stillwater.app/Contents/MacOS/Stillwater'
    report = {'machine': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'os': platform.mac_ver()[0], 'sha256': {}, 'cases': {},
              'note': 'Matched native desktop 4x MSAA, 24fps, muted; four-second warmup, 12-second CPU interval. GPU command timings include startup. No power inference.'}
    for path in [executable, root / 'assets/aquarium.metal', root / 'assets/camera_registry.metal']:
        report['sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for label, arguments in [('generic-before', ['--generic-foliage']), ('specialized-first', []),
                             ('record-shading', ['--record-shading']), ('specialized-second', []),
                             ('generic-after', ['--generic-foliage'])]:
        result = measure(root, label, ['--desktop', '--muted', *arguments], 12, executable, output)
        report['cases'][label] = result
        (output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')
        print(label, result['mean_gpu_ms_per_timed_frame'], result['gpu_allocated_bytes'], flush=True)


if __name__ == '__main__':
    main()
