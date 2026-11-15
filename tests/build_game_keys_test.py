"""With content_keys, a change to any file under runtime/ (an included .c) changes every runtime compile command, and a
different prebuilt runtime library changes the link command. Run with the repo's Python: tests/build_game_keys_test.py
"""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import build_game  # noqa: E402  # type: ignore

with tempfile.TemporaryDirectory() as d:
    rt, out, gen = Path(d) / 'runtime', Path(d) / 'out', Path(d) / 'gen'
    for p in (rt, out, gen):
        p.mkdir()
    (rt / 'helper.c').write_text('#include "draw.c"\n')
    (rt / 'draw.c').write_text('int a;\n')
    lib = Path(d) / 'libtotkrt.a'
    lib.write_bytes(b'one')
    build_game.RUNTIME = rt

    def ninja(**kw):
        return build_game.ninja_file(gen, out, {}, 4, content_keys=True, **kw)[0]

    def helper_cmd(text):
        i = text.index('helper.c')
        return text[i:].split('build ', 1)[0]

    before = helper_cmd(ninja())
    (rt / 'draw.c').write_text('int b;\n')
    assert helper_cmd(ninja()) != before, 'an included .c must change the including source\'s command'

    def link(text):
        return text[text.index('rule link_lib'):].split('\n', 2)[1]

    first = link(ninja(runtime_lib=lib))
    lib.write_bytes(b'two')
    assert link(ninja(runtime_lib=lib)) != first, 'a different library must change the link command'
print('ok')
