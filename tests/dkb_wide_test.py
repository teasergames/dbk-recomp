"""Builds and runs tests/dkb_wide_test.c (runtime/dkb_wide.c) and tests/res_heapdef_test.c (runtime/resolution.c); no game data."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    ap.add_argument('--opt', choices=('0', '2'), default='2')
    ap.add_argument('--out', type=Path, default=ROOT / 'build/tests/dkb-wide')
    args = ap.parse_args()
    out = args.out.resolve()
    out.relative_to((ROOT / 'build').resolve())
    out.mkdir(parents=True, exist_ok=True)
    for name, libs in (('dkb_wide_test', []), ('res_heapdef_test', ['-lzstd'])):
        exe = out / (name + '.exe')
        command = [args.cc, '-std=gnu11', '-O' + args.opt, '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-Wno-sign-compare', '-Wno-missing-field-initializers', '-I', ROOT / 'runtime',
                   ROOT / 'tests' / (name + '.c'), '-o', exe, *libs]
        compiled = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120)
        if compiled.returncode:
            print(compiled.stdout + compiled.stderr)
            return compiled.returncode
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        print(result.stdout + result.stderr, end='')
        if result.returncode:
            return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
