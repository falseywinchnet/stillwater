#!/usr/bin/env python3
"""Build and measure the isolated two-projection MSAA trial on a Mac."""
import argparse
from pathlib import Path
import subprocess

BASE = '0cd481f6d42435855801d615c15ef84ad4178228'


def command(arguments, cwd, log):
    with log.open('w') as stream:
        subprocess.run(arguments, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    experiment = Path(__file__).resolve().parent
    root = experiment.parents[1]
    output = (args.output or root / 'artifacts/paired-view-reproduction').resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'
    source.mkdir()
    archive = output / 'source.tar'
    subprocess.run(['git', 'archive', '--format=tar', '--output=' + str(archive), BASE,
                    'CMakeLists.txt', 'src', 'include', 'assets', 'tests'], cwd=root, check=True)
    subprocess.run(['tar', '-xf', str(archive), '-C', str(source)], check=True)
    archive.unlink()
    command(['patch', '-p1', '--batch', '--forward', '-i', str(experiment / 'prototype.patch')],
            source, output / 'patch.log')
    build = output / 'build'
    command(['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'],
            source, output / 'configure.log')
    command(['cmake', '--build', str(build), '-j', '4'], source, output / 'build.log')
    executable = build / 'Stillwater.app/Contents/MacOS/Stillwater'
    flags = [str(executable), '--desktop', '--muted', '--full-redraw', '--msaa', '4']
    if not args.build_only:
        for name in ['paired', 'paired-repeat']:
            image = output / (name + '.png')
            command(flags + ['--paused', '--no-leaf-grain', '--capture-time', '8',
                             '--capture', str(image), '--quit-after', '.2',
                             '--metrics', str(output / (name + '.json'))], output, output / (name + '.log'))
            if not image.is_file():
                raise RuntimeError('Capture failed: inspect ' + str(output / (name + '.log')))
        if (output / 'paired.png').read_bytes() != (output / 'paired-repeat.png').read_bytes():
            raise RuntimeError('Repeated fixed-time capture differs')
        command(flags + ['--quit-after', str(args.seconds), '--metrics', str(output / 'animated.json')],
                output, output / 'animated.log')
    print('Experimental executable: ' + str(executable))
    print('Live arguments: --desktop --full-redraw --msaa 4')


if __name__ == '__main__':
    main()
