#!/usr/bin/env python3
"""Compare saved and current app bundles on the same Mac at unchanged settings."""
import argparse
import hashlib
import json
import pathlib
import platform
import subprocess

from measure import measure


def fingerprint(executable):
    resources = executable.parent.parent / "Resources"
    paths = [executable, resources / "aquarium.metal", resources / "riverscape.swscene.gz"]
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=pathlib.Path, required=True,
                        help="Saved baseline .app bundle with its original Resources")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("artifacts/neo-storage"))
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be between 1 and 3600")
    root = pathlib.Path(__file__).resolve().parents[1]
    baseline = args.baseline.resolve() / "Contents/MacOS/Stillwater"
    current = root / "build/Stillwater.app/Contents/MacOS/Stillwater"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "machine": subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip(),
        "physical_memory_bytes": int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True)),
        "os": platform.mac_ver()[0],
        "baseline_sha256": fingerprint(baseline),
        "current_sha256": fingerprint(current),
        "note": "Desktop, 24 fps, 4x MSAA, unchanged dimensions. Four seconds warmup. GPU command intervals include startup; WindowServer includes other apps. RSS and Metal allocation overlap on unified memory. No power measurement.",
        "cases": {},
    }
    cases = [("baseline-before", baseline), ("compact-first", current),
             ("compact-second", current), ("baseline-after", baseline)]
    for label, executable in cases:
        result = measure(root, label, ["--desktop", "--muted"], args.seconds, executable, output)
        report["cases"][label] = result
        (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
        print(label, json.dumps(result), flush=True)
    expected = report["cases"]["baseline-before"]
    for result in report["cases"].values():
        for key in ["render_width", "render_height", "msaa_samples", "requested_fps", "render_scale"]:
            if result[key] != expected[key]:
                raise RuntimeError("Comparison changed visual setting: " + key)


if __name__ == "__main__":
    main()
