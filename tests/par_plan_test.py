"""Run the planner of the helper threads' parts (runtime/vk_exec.c par_plan, par_count) without a device.

The fixture gets the production PAR_* constants, the ParSlot type and the functions as text, unchanged, and checks the
helper counts docs/architecture.md gives, plans worked out by hand and properties every plan has. Use --source-root on
another checkout to run the same cases against its planner.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, declaration):
    start = source.index(declaration)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default=shutil.which('clang') or 'clang')
    ap.add_argument('--source-root', type=Path, default=ROOT)
    args = ap.parse_args()
    prod = (args.source_root / 'runtime/vk_exec.c').read_text()
    helpers = re.search(r'^#define VK_SPEC_HELPERS \d+', (args.source_root / 'runtime/vk_spec.h').read_text(), re.M).group(0)
    constants = prod[prod.index('#define PAR_RUN_MAX'):prod.index('// What is known about one run.')]
    slot_end = prod.index('} ParSlot;') + len('} ParSlot;')
    slot = prod[prod.rindex('typedef struct {', 0, slot_end):slot_end]
    code = '\n\n'.join([constants, slot] + [function(prod, 'static ' + decl + '(') for decl in
                                            ('void par_bit', 'int par_bit_at', 'uint32_t par_count', 'uint32_t par_plan')])
    fixture = (ROOT / 'tests/par_plan_test.c').read_text()
    fixture = fixture.replace('/* VK_SPEC_HELPERS is inserted here by par_plan_test.py */', helpers)
    fixture = fixture.replace('/* production code is inserted here by par_plan_test.py */', code)
    with tempfile.TemporaryDirectory(prefix='dkb-par-plan-') as tmp:
        src = Path(tmp) / 'test.c'
        src.write_text(fixture)
        for opt in ('-O0', '-O2'):
            exe = Path(tmp) / ('par_plan' + opt[2:] + '.exe')
            subprocess.run([args.cc, '-std=gnu11', opt, '-Wall', str(src), '-o', str(exe), '-lm'], check=True)
            subprocess.run([str(exe)], check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
