#!/usr/bin/env python3
"""Reproduce the isolated numerical visibility trial; never edits the main build."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

BASE = '0cd481f6d42435855801d615c15ef84ad4178228'


def command(arguments, cwd, log):
    with log.open('w') as stream:
        subprocess.run(arguments, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=True)


def capture(executable, mode, output, label):
    image = output / (label + '.png')
    command([str(executable), '--desktop', '--muted', '--paused', '--no-leaf-grain',
             '--aa', mode, '--capture-time', '8', '--capture', str(image),
             '--metrics', str(output / (label + '.json')), '--quit-after', '0.2'],
            output, output / (label + '.log'))
    if not image.is_file():
        raise RuntimeError('Capture failed; inspect ' + str(output / (label + '.log')))
    if mode == 'integrate' and 'overflow: 0' not in (output / (label + '.log')).read_text():
        raise RuntimeError('Integration capacity was exceeded or counters are missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--seconds', type=float, default=8)
    args = parser.parse_args()
    experiment = Path(__file__).resolve().parent
    root = experiment.parents[1]
    output = (args.output or root / 'artifacts/pixel-integration-reproduction').resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'
    source.mkdir()
    archive = output / 'source.tar'
    subprocess.run(['git', 'archive', '--format=tar', '--output=' + str(archive), BASE,
                    'CMakeLists.txt', 'src', 'include', 'assets', 'tests'], cwd=root, check=True)
    subprocess.run(['tar', '-xf', str(archive), '-C', str(source)], check=True)
    archive.unlink()
    executables = {}
    for label in ['baseline', 'integration']:
        if label == 'integration':
            command(['patch', '-p1', '--batch', '--forward', '-i', str(experiment / 'prototype.patch')],
                    source, output / 'patch.log')
            shutil.copyfile(experiment / 'integration.metal', source / 'assets/integration.metal')
        build = output / ('build-' + label)
        command(['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'],
                source, output / ('configure-' + label + '.log'))
        command(['cmake', '--build', str(build), '-j', '4'], source, output / ('build-' + label + '.log'))
        executables[label] = build / 'Stillwater.app/Contents/MacOS/Stillwater'
    if not args.build_only:
        for label, mode in [('baseline', 'msaa'), ('integration', 'integrate')]:
            capture(executables[label], mode, output, label)
            command([str(executables[label]), '--desktop', '--muted', '--no-leaf-grain',
                     '--aa', mode, '--quit-after', str(args.seconds),
                     '--metrics', str(output / ('active-' + label + '.json'))],
                    output, output / ('active-' + label + '.log'))
        capture(executables['integration'], 'integrate', output, 'integration-repeat')
        if (output / 'integration.png').read_bytes() != (output / 'integration-repeat.png').read_bytes():
            raise RuntimeError('Repeated integration PNG differs; inspect the images')
    print(json.dumps({'base_commit': BASE, 'output': str(output),
                      'executables': {key: str(value) for key, value in executables.items()}}, indent=2))


if __name__ == '__main__':
    main()
