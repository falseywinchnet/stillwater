#!/usr/bin/env python3
"""Check the bundled reproduction inputs without downloading assets."""
import hashlib
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1] / 'assets'
    checked = 0
    for line in (root / 'SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        path = root / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit('Asset hash mismatch: ' + name)
        checked += 1
    print(f'{checked} bundled assets verified')


if __name__ == '__main__':
    main()
