#!/usr/bin/env python3
"""Measure warm CPU independently of launch/shader compilation. No root required."""
import argparse
import hashlib
import json
import pathlib
import platform
import subprocess
import time


def process_time(pid):
    result = subprocess.run(["ps", "-p", str(pid), "-o", "time=,rss="], text=True, capture_output=True, check=True)
    clock, rss = result.stdout.split()
    parts = clock.split(":")
    seconds = 0.0
    for part in parts:
        seconds = seconds * 60 + float(part)
    return seconds, int(rss) * 1024


def window_server():
    lines = subprocess.check_output(["ps", "-A", "-o", "pid=,comm="], text=True).splitlines()
    for line in lines:
        pid, command = line.strip().split(None, 1)
        if command.endswith("/WindowServer"):
            return int(pid)
    raise RuntimeError("WindowServer not found")


def measure(root, label, arguments, seconds):
    metrics = root / "artifacts" / (label + ".json")
    executable = root / "build/Stillwater.app/Contents/MacOS/Stillwater"
    command = [str(executable), "--preview", "--metrics", str(metrics), "--quit-after", str(seconds + 7), *arguments]
    with (root / "artifacts" / (label + ".log")).open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            time.sleep(4)
            if process.poll() is not None:
                raise RuntimeError(f"{label} ended early; read its log")
            server = window_server()
            app_before, _ = process_time(process.pid)
            server_before, _ = process_time(server)
            begin = time.monotonic()
            time.sleep(seconds)
            app_after, rss = process_time(process.pid)
            server_after, _ = process_time(server)
            elapsed = time.monotonic() - begin
            process.wait(timeout=15)
            if process.returncode:
                raise RuntimeError(f"{label} exited {process.returncode}")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    result = json.loads(metrics.read_text())
    result.update(warm_seconds=elapsed, warm_cpu_percent_one_core=100 * (app_after-app_before)/elapsed,
                  concurrent_windowserver_cpu_percent_one_core=100 * (server_after-server_before)/elapsed,
                  warm_rss_bytes=rss)
    if result["gpu_timed_frames"]:
        result["mean_gpu_ms_per_timed_frame"] = 1000*result["gpu_executed_seconds"]/result["gpu_timed_frames"]
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--compare-gpu", action="store_true",
                        help="Compare optional quality/rate settings; defaults stay unchanged")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be between 1 and 3600")
    root = pathlib.Path(__file__).resolve().parents[1]
    (root / "artifacts").mkdir(exist_ok=True)
    report = {"machine": subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip(),
              "os": platform.mac_ver()[0], "note": "One-core CPU deltas from ps, 4 s warmup. WindowServer includes other apps. GPU command duration is not total GPU power.", "cases": {}}
    report["sha256"] = {
        str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [root / "build/Stillwater.app/Contents/MacOS/Stillwater",
                     root / "assets/aquarium.metal", root / "assets/riverscape.swscene.gz"]
    }
    server = window_server()
    before, _ = process_time(server)
    start = time.monotonic()
    time.sleep(args.seconds)
    after, _ = process_time(server)
    report["windowserver_baseline_cpu_percent_one_core"] = 100*(after-before)/(time.monotonic()-start)
    cases = [("animated-muted", ["--muted"]), ("animated-sound", []),
             ("paused-muted", ["--paused", "--muted"])]
    if args.compare_gpu:
        cases = [("gpu-reference", []), ("gpu-two-samples", ["--msaa", "2"]),
                 ("gpu-three-quarter-size", ["--render-scale", "0.75"]),
                 ("gpu-eighteen-fps", ["--fps", "18"])]
        cases = [(label, ["--muted", "--capture", str(root / "artifacts" / (label + ".png")), *arguments])
                 for label, arguments in cases]
    for label, arguments in cases:
        report["cases"][label] = measure(root, label, arguments, args.seconds)
        print(label, json.dumps(report["cases"][label]), flush=True)
    filename = "gpu-comparison.json" if args.compare_gpu else "performance.json"
    (root / "artifacts" / filename).write_text(json.dumps(report, indent=2)+"\n")


if __name__ == "__main__":
    main()
