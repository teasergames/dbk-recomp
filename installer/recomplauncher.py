"""Headless Bananza setup with RecompLauncher's line-based progress protocol.

The tool package stays at the release root; the existing installer owns only game/.
No extraction, build, validation or resume rules are duplicated here.
"""
import argparse
import sys
from pathlib import Path
from types import SimpleNamespace

from . import bananza
from .events import Reporter, render
from .process_job import keep_children_in_job


class LauncherReporter(Reporter):
    def __init__(self, out=None):
        super().__init__(out=out)
        self.steps = []
        self.fraction = 0.0

    def emit(self, type, **fields):
        event = {'type': type, **fields}
        self.log(self.describe(event))
        message = None
        fraction = self.fraction
        if type == 'start':
            self.steps = fields['steps']
            message = 'Checking your Bananza dump'
        elif type in ('step', 'progress') and fields['step'] in self.steps:
            index = self.steps.index(fields['step'])
            part = 0.0
            if type == 'step':
                part = float(fields['state'] in ('done', 'skipped'))
                message = render(self.catalog, 'step.' + fields['step'])
            else:
                # A step can contain several phases. Keep the overall fraction monotonic.
                total = fields.get('total', 0)
                if total:
                    part = min(0.95, max(0.0, fields.get('done', 0) / total))
                message = render(self.catalog, f"phase.{fields['step']}.{fields['phase']}")
            fraction = min(0.99, (index + part) / len(self.steps))
        elif type in ('error', 'warning'):
            message = f"{type.capitalize()}: {render(self.catalog, fields['id'], fields.get('params'))}"
        elif type == 'end':
            if fields['result'] == 'ok':
                fraction, message = 1.0, 'Bananza setup complete'
            else:
                message = 'Bananza setup cancelled' if fields['result'] == 'cancelled' else 'Bananza setup failed'
        if message is not None:
            self.fraction = max(self.fraction, min(1.0, fraction))
            # Paths and exception text can contain line breaks; never let them forge progress records.
            message = ' '.join(message.splitlines())
            self.out.write(f'progress {self.fraction:.4f} {message}\n')
            self.out.flush()
        return event


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--exefs', type=Path, required=True)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    options = SimpleNamespace(
        root=root, dest=root / 'game', temp=None, package=None,
        exefs=args.exefs, romfs=args.romfs, jobs=args.jobs, lang=None, preserve_language=True, verify=False,
        base=None, update=None, keys=None, toolchain=None, deps=None, ninja=None,
        dll_dir=None, translator=None, dotnet=None)
    reporter = LauncherReporter()
    try:
        keep_children_in_job()
    except OSError as error:
        reporter.error('internal', {'exception': type(error).__name__, 'detail': str(error)})
        return 1
    return bananza.main(options, reporter=reporter)


if __name__ == '__main__':
    sys.exit(main())
