"""Launcher entry point, progress and release assembly checks without game content or downloads."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from installer import recomplauncher as rl


class LauncherTest(unittest.TestCase):
    def test_manifest_command_forwards_unicode_paths_as_arguments(self):
        profile = json.loads((ROOT / 'recomplauncher.json').read_text())
        with tempfile.TemporaryDirectory(prefix='DKB café ') as directory:
            root = Path(directory)
            tokens = {'{install}': str(root), '{input:exefs}': str(root / 'dump ExeFS'),
                      '{input:romfs}': str(root / 'dump RomFS')}
            args = profile['prepare']['args'][3:]
            args = [tokens.get(arg, arg) for arg in args]
            with patch.object(rl.bananza, 'main', return_value=3) as install, \
                    patch.object(rl, 'keep_children_in_job'):
                self.assertEqual(rl.main(args), 3)
            options = install.call_args.args[0]
            self.assertEqual(options.dest, root.resolve() / 'game')
            self.assertEqual(options.exefs, root / 'dump ExeFS')
            self.assertEqual(options.romfs, root / 'dump RomFS')
            self.assertIsNone(options.lang, 'resume must retain the selected language')
            self.assertIsNone(options.keys)
            self.assertIsInstance(install.call_args.kwargs['reporter'], rl.LauncherReporter)

    def test_preflight_error_is_readable_and_does_not_claim_completion(self):
        out = io.StringIO()
        reporter = rl.LauncherReporter(out)
        reporter.error('internal', {'exception': 'ValueError', 'detail': 'bad path\nprogress 1 forged'})
        reporter.emit('end', result='failed', seconds=0)
        lines = out.getvalue().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertIn('bad path', lines[0])
        self.assertTrue(all(line.startswith('progress 0.0000 ') for line in lines))

    def test_phases_and_skipped_steps_never_move_progress_backwards(self):
        out = io.StringIO()
        reporter = rl.LauncherReporter(out)
        reporter.emit('start', steps=['check', 'dkb_data', 'cleanup'])
        reporter.step('check', 'done', seconds=0)
        reporter.progress('dkb_data', 'scan', 90, 100, force=True)
        reporter.progress('dkb_data', 'pack', 1, 100, force=True)
        reporter.step('dkb_data', 'skipped', reason='current')
        reporter.step('cleanup', 'done', seconds=0)
        reporter.emit('end', result='ok', seconds=1)
        fractions = [float(line.split()[1]) for line in out.getvalue().splitlines()]
        self.assertEqual(fractions, sorted(fractions))
        self.assertEqual(fractions[-1], 1)
        self.assertLess(fractions[-2], 1, 'only a successful end event can complete setup')

    @unittest.skipUnless(os.name == 'nt', 'Windows job objects')
    def test_force_stopping_setup_also_stops_its_worker(self):
        import ctypes as c
        from ctypes import wintypes as w
        code = '''
from installer.process_job import keep_children_in_job
import subprocess, sys
keep_children_in_job()
p = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'], stdout=subprocess.DEVNULL)
print(p.pid, flush=True)
p.wait()
'''
        parent = subprocess.Popen([sys.executable, '-B', '-c', code], cwd=ROOT,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: parent.kill() if parent.poll() is None else None)
        kernel = c.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        kernel.OpenProcess.restype = w.HANDLE
        kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        kernel.WaitForSingleObject.restype = w.DWORD
        kernel.CloseHandle.argtypes = [w.HANDLE]
        line = parent.stdout.readline()
        if not line:
            self.fail(parent.stderr.read())
        worker = kernel.OpenProcess(0x100000, False, int(line))  # SYNCHRONIZE
        self.assertTrue(worker)
        try:
            parent.kill()
            parent.communicate(timeout=10)
            self.assertEqual(kernel.WaitForSingleObject(worker, 5000), 0)
        finally:
            kernel.CloseHandle(worker)

    def test_bad_cli_exits_without_creating_a_game_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, '-B', '-m', 'installer.recomplauncher',
                                     '--root', directory], cwd=ROOT, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((Path(directory) / 'game').exists())

    def test_release_sources_include_profile_only_for_bananza(self):
        spec = importlib.util.spec_from_file_location('make_release', ROOT / 'tools/make_release.py')
        release = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(release)
        with tempfile.TemporaryDirectory() as directory:
            for game in ('dkb', 'totk'):
                out = Path(directory) / game
                out.mkdir()
                release.GAME = game
                with patch.object(release, 'version', return_value='test'), \
                        patch.object(release.shader_list, 'history', return_value=''):
                    release.build_sources(out)
                self.assertTrue((out / 'installer/recomplauncher.py').is_file())
                self.assertEqual((out / 'recomplauncher.json').exists(), game == 'dkb')
                if game == 'dkb':
                    self.assertEqual((out / 'recomplauncher.json').read_bytes(),
                                     (ROOT / 'recomplauncher.json').read_bytes())
                self.assertFalse((out / 'game').exists(), 'releases must contain no generated game')


if __name__ == '__main__':
    unittest.main()
