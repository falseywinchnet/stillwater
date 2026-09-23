#!/usr/bin/env python3
"""Compare deterministic native captures. Requires Pillow; no screen capture."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    native = json.loads((args.directory / "native-verification.json").read_text())
    report = {"native": native, "pairs": {}, "passed": native["failures"] == 0}
    for retained in sorted(args.directory.glob("*-retained.png")):
        name = retained.name.removesuffix("-retained.png")
        full = args.directory / (name + "-full.png")
        first = Image.open(retained).convert("RGB")
        second = Image.open(full).convert("RGB")
        if first.size != second.size:
            raise RuntimeError("Mismatched dimensions: " + name)
        difference = ImageChops.difference(first, second)
        histogram = difference.histogram()
        count = first.width * first.height * 3
        total = 0
        over_eight = 0
        squared = 0
        maximum = 0
        for channel in range(3):
            for value in range(256):
                frequency = histogram[channel * 256 + value]
                total += value * frequency
                squared += value * value * frequency
                if frequency:
                    maximum = max(maximum, value)
                if value > 8:
                    over_eight += frequency
        # Half-precision cached coefficients permit small quantization errors.
        # Large localized stale regions fail separately from mean image error.
        mean = total / count
        fraction = over_eight / count
        passed = mean < 0.01 and maximum <= 8
        report["passed"] = report["passed"] and passed
        report["pairs"][name] = {"size": list(first.size), "mean_absolute_255": mean,
                                  "rms_255": (squared / count) ** 0.5,
                                  "fraction_over_eight": fraction, "maximum_255": maximum,
                                  "passed": passed,
                                  "retained_sha256": hashlib.sha256(retained.read_bytes()).hexdigest(),
                                  "full_sha256": hashlib.sha256(full.read_bytes()).hexdigest()}
    if len(report["pairs"]) != native["comparisons"] or not report["pairs"]:
        report["passed"] = False
    if (args.directory / 'leaf-grain-restored-retained.png').exists():
        original = Image.open(args.directory / 'initial-retained.png').convert('RGB')
        disabled = Image.open(args.directory / 'leaf-grain-off-retained.png').convert('RGB')
        restored = Image.open(args.directory / 'leaf-grain-restored-retained.png').convert('RGB')
        if original.size != disabled.size or original.size != restored.size:
            raise RuntimeError('Leaf toggle comparison dimensions changed')
        changed = ImageChops.difference(original, disabled).getbbox() is not None
        exact = ImageChops.difference(original, restored).getbbox() is None
        report['leaf_toggle'] = {'changes_image': changed, 'restores_exact_image': exact}
        report['passed'] = report['passed'] and changed and exact
    (args.directory / "image-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
