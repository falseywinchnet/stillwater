#!/usr/bin/env python3
"""Run the native Metal validation suite at both supported MSAA sample counts."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path, default=Path('artifacts/native-verification'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    executable = args.build_dir.resolve() / 'Stillwater.app/Contents/MacOS/Stillwater'
    environment = dict(os.environ, MTL_DEBUG_LAYER='1')
    for samples in [2, 4]:
        output = args.output.resolve() / f'{samples}x'
        subprocess.run([str(executable), '--preview', '--muted', '--msaa', str(samples),
                        '--verify-retained', str(output)], env=environment, check=True)
        subprocess.run([sys.executable, str(root / 'tools/compare_retained.py'), str(output)], check=True)
    print('Both native sample-count suites passed')


if __name__ == '__main__':
    main()
