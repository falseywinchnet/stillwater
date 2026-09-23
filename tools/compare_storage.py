#!/usr/bin/env python3
"""Compare compact-cache images and world queries with a saved original renderer."""
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def image_error(first, second):
    with Image.open(first) as source, Image.open(second) as target:
        if source.size != target.size:
            raise RuntimeError("Image dimensions changed: " + first.name)
        histogram = ImageChops.difference(source.convert("RGB"), target.convert("RGB")).histogram()
        total = sum((index % 256) * count for index, count in enumerate(histogram))
        maximum = max(index % 256 for index, count in enumerate(histogram) if count)
        return {"mean_absolute_255": total / (source.width * source.height * 3),
                "maximum_255": maximum}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    args = parser.parse_args()
    report = {"passed": True, "images": {}, "probes": {}}
    original = json.loads((args.baseline / "native-verification.json").read_text())
    current = json.loads((args.current / "native-verification.json").read_text())
    if original["failures"] or current["failures"] or original["comparisons"] != current["comparisons"]:
        raise RuntimeError("Native verification failed or comparison counts differ")
    paths = sorted(args.baseline.glob("*-probes.json"))
    if len(paths) != original["comparisons"] or not paths:
        raise RuntimeError("Missing baseline visibility probes")
    for path in paths:
        name = path.name.removesuffix("-probes.json")
        for kind in ["full", "retained"]:
            filename = name + "-" + kind + ".png"
            error = image_error(args.baseline / filename, args.current / filename)
            # Changing storage does not change the full-redraw reference image.
            passed = error["maximum_255"] == 0 if kind == "full" else (
                error["maximum_255"] <= 8 and error["mean_absolute_255"] < 0.01)
            error["passed"] = passed
            report["passed"] = report["passed"] and passed
            report["images"][filename] = error
        first = json.loads(path.read_text())
        second = json.loads((args.current / path.name).read_text())
        if len(first) != len(second) or not first:
            raise RuntimeError("Probe counts differ: " + name)
        maximum = 0.0
        identities = 0
        depth_error = 0.0
        occupied = 0
        for before, after in zip(first, second):
            if before["pixel"] != after["pixel"] or len(before["samples"]) != 4 or len(after["samples"]) != 4:
                raise RuntimeError("Probe layout changed: " + name)
            for probe in [before, after]:
                if len(probe["depths"]) != 4:
                    raise RuntimeError("Probe depths incomplete: " + name)
                for sample in probe["samples"]:
                    if len(sample) != 4 or not all(math.isfinite(value) for value in sample):
                        raise RuntimeError("Invalid world probe: " + name)
                if not all(math.isfinite(value) for value in probe["depths"]):
                    raise RuntimeError("Invalid raster depth: " + name)
            depth_error = max(depth_error, max(abs(a - b) for a, b in zip(before["depths"], after["depths"])))
            for sample, target in zip(before["samples"], after["samples"]):
                identities += sample[3] != target[3]
                if sample[3]:
                    occupied += 1
                    maximum = max(maximum, max(abs(a - b) for a, b in zip(sample[:3], target[:3])))
                elif target != [0, 0, 0, 0]:
                    raise RuntimeError("Background probe changed: " + name)
        # Half-stored interpolation corrections retain subpixel receiver positions.
        # Bound the remaining float roundoff separately from exact identity/depth
        # and the existing renderer's image-comparison tolerances.
        passed = identities == 0 and depth_error == 0 and maximum < 0.00002 and occupied > 0
        report["passed"] = report["passed"] and passed
        report["probes"][name] = {"maximum_world_component_error": maximum,
                                  "identity_mismatches": identities, "depth_error": depth_error,
                                  "occupied_samples": occupied, "passed": passed}
    (args.current / "storage-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
