"""Run the helper threads' handoff (runtime/vk_spec.c vk_spec_launch, vk_spec_join) without a device.

The production file is compiled unchanged with the real platform layer, so the helper threads are real threads. The
fixture stands in for the configuration, the log, the Vulkan context and the three Vulkan calls the file makes. Each
row of CASES is one process, because vk_spec_on reads its knobs once: the value of TOTK_REPLAY_PAR, the value of
TOTK_REPLAY_HELPERS ('-': not set) and the helper threads that must run ('cpu': what the logical processors give).
A last process checks vk_spec_helpers_for, the count without TOTK_REPLAY_HELPERS, for every number of logical
processors.
Use --source-root on another checkout to run the same cases against its vk_spec.c.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    ('-', '-', '0'),        # off by default
    ('0', '5', '0'),
    ('-', '5', '0'),
    ('1', '1', '1'),
    ('1', '2', '2'),
    ('1', '5', '5'),
    ('1', '9', '5'),        # at most VK_SPEC_HELPERS
    ('1', '-', 'cpu'),
    ('check', '3', '3'),
    ('ab', '2', '2'),
)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default=shutil.which('clang') or 'clang')
    ap.add_argument('--deps', type=Path, required=True, help='the dependency folder whose include/ holds vulkan/ and vma/')
    ap.add_argument('--source-root', type=Path, default=ROOT)
    args = ap.parse_args()
    runtime = args.source_root / 'runtime'
    platform = 'win32.c' if os.name == 'nt' else 'linux.c'
    libraries = ['-lbcrypt', '-lpdh', '-lpsapi', '-lrstrtmgr', '-lshell32', '-lwinmm'] if os.name == 'nt' else ['-ldl', '-pthread']
    include = args.deps / 'include'
    with tempfile.TemporaryDirectory(prefix='dkb-vk-spec-') as tmp:
        for opt in ('-O0', '-O2'):
            exe = Path(tmp) / ('vk_spec_handoff' + opt[2:] + '.exe')
            subprocess.run([args.cc, '-std=gnu11', opt, '-Wall', '-ffunction-sections', '-fdata-sections',
                            '-I', str(runtime), '-I', str(include), '-I', str(include / 'vma'),
                            str(ROOT / 'tests/vk_spec_handoff_test.c'), str(runtime / 'vk_spec.c'),
                            str(runtime / 'platform' / platform), str(runtime / 'platform/common.c'),
                            '-Wl,--gc-sections', *libraries, '-o', str(exe)], check=True)
            for case in CASES:
                subprocess.run([str(exe), *case], check=True, timeout=120)
            subprocess.run([str(exe), 'counts'], check=True, timeout=120)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
