"""Compile the real config reader with platform stubs; check launcher precedence without a game/GPU."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

PLATFORM = r'''
#include <stdint.h>
#include <stddef.h>
typedef struct { void *p; } PlatMutex;
#define PLAT_MUTEX_INIT {0}
void plat_mutex_lock(PlatMutex *m);
void plat_mutex_unlock(PlatMutex *m);
uint8_t *plat_file_read(const char *, uint64_t *);
int plat_file_exists(const char *);
int64_t plat_file_size(const char *);
int plat_file_write_atomic(const char *, const void *, size_t);
int plat_exe_path(char *, size_t);
char *plat_last_sep(const char *);
'''

STUBS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"
#include "cfg.h"
int rt_game_version;
void rt_log(const char *fmt, ...) { (void)fmt; }
void rt_fatal(const char *fmt, ...) { (void)fmt; abort(); }
void plat_mutex_lock(PlatMutex *m) { (void)m; }
void plat_mutex_unlock(PlatMutex *m) { (void)m; }
uint8_t *plat_file_read(const char *path, uint64_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *p = calloc((size_t)n + 1, 1);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) abort();
    fclose(f); *size = (uint64_t)n; return p;
}
int plat_file_exists(const char *path) { (void)path; return 0; }
int64_t plat_file_size(const char *path) { (void)path; return 0; }
int plat_file_write_atomic(const char *path, const void *data, size_t n) {
    (void)path; (void)data; (void)n; return 1;
}
int plat_exe_path(char *out, size_t cap) { (void)out; (void)cap; return 0; }
char *plat_last_sep(const char *path) { (void)path; return NULL; }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    rt_game_version = atoi(argv[1]);
    if (!cfg_load(argv[2])) return 3;
    printf("%s %d %d\n", cfg_value("TOTK_FULLSCREEN"),
           cfg_is_set("TOTK_FULLSCREEN"), cfg_from_env("TOTK_FULLSCREEN"));
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cc-arg', action='append', default=[], help='additional compiler/linker argument')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='dkb-cfg-test-') as directory:
        work = Path(directory)
        (work / 'platform').mkdir()
        # Copy the production reader unchanged; stubs replace only OS/host boundaries.
        for name in ('cfg.c', 'cfg.h'):
            shutil.copyfile(ROOT / 'runtime' / name, work / name)
        (work / 'host.h').write_text('extern int rt_game_version;\nvoid rt_log(const char *, ...);\n'
                                    'void rt_fatal(const char *, ...);\n')
        (work / 'platform/platform.h').write_text(PLATFORM)
        (work / 'check.c').write_text(STUBS)
        exe = work / ('check.exe' if os.name == 'nt' else 'check')
        command = [args.cc, '-std=gnu11', '-O1', str(work / 'cfg.c'), str(work / 'check.c'), '-o', str(exe)]
        if os.name == 'nt':
            command += ['-Dstrcasecmp=_stricmp', '-Dstrdup=_strdup', '-D_CRT_SECURE_NO_WARNINGS']
        command += args.cc_arg
        subprocess.run(command, check=True)
        base = {k: v for k, v in os.environ.items()
                if not k.upper().startswith(('DKB_', 'TOTK_', 'RECOMPLAUNCHER')) and k.upper() != 'STEAMDECK'}
        big = {'RECOMPLAUNCHER': '1', 'RECOMPLAUNCHER_MODE': 'bigpicture'}
        cases = [
            ('standalone', {}, '', 0, '0 0 0'),
            ('desktop', {'RECOMPLAUNCHER': '1', 'RECOMPLAUNCHER_MODE': 'desktop'}, '', 0, '0 0 0'),
            ('big picture', big, '', 0, '1 0 0'),
            ('mode alone', {'RECOMPLAUNCHER_MODE': 'bigpicture'}, '', 0, '0 0 0'),
            ('disabled launcher', {**big, 'RECOMPLAUNCHER': '0'}, '', 0, '0 0 0'),
            ('saved window', big, '[display]\nfullscreen = 0\n', 0, '0 1 0'),
            ('saved full screen', {}, '[display]\nfullscreen = 1\n', 0, '1 1 0'),
            ('automatic', big, '[display]\nfullscreen =\n', 0, '1 0 0'),
            ('env window', {**big, 'TOTK_FULLSCREEN': '0'}, '', 0, '0 1 1'),
            ('alias wins', {**big, 'DKB_FULLSCREEN': '0', 'TOTK_FULLSCREEN': '1'}, '', 0, '0 1 1'),
            ('env beats ini', {**big, 'DKB_FULLSCREEN': '0'}, 'fullscreen = 1\n', 0, '0 1 1'),
            ('TotK unchanged', big, '', 1, '0 0 0'),
            ('device profile', {'SteamDeck': '1'}, '', 0, '1 0 0'),
            ('saved window beats device', {**big, 'SteamDeck': '1'}, 'fullscreen = 0\n', 0, '0 1 0'),
        ]
        for label, env, text, game_version, expected in cases:
            ini = work / 'dkb.ini'
            ini.write_text(text)
            result = subprocess.run([str(exe), str(game_version), str(ini)], env={**base, **env},
                                    capture_output=True, text=True, check=True)
            assert result.stdout.strip() == expected, (label, result.stdout, result.stderr)
            assert ini.read_text() == text, f'{label}: session default must not write settings'
        print(f'Launcher config: {len(cases)} cases passed')


if __name__ == '__main__':
    main()
