"""tools/extract_nx.py on a tiny synthetic NX archive built here (raw and zstd blocks, a base and an update snapshot,
an add-on); no game data. Runs it through extract_nup's install_plan, extract_tree, pack_romfs and write_aoc.

Run: python tests/nx_extract_test.py
"""
import contextlib
import hashlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import extract_nup as nup  # noqa: E402
import extract_nx as nx  # noqa: E402
from nup_extract_check import zstd_compress  # noqa: E402

APP = 0x0100AAAA00BBB000
S = hashlib.sha256


def build(snapshots, use_zstd):
    """snapshots: list of (type, version, flags, content_id, tree) where tree is {name: bytes | tree}; its top level
    holds exefs and romfs. One block per file (the files here are small)."""
    dirs, entries, files, refs, blocks, pool, data, checks = [], [], [], [], [], bytearray(), bytearray(), bytearray()

    def name_ref(name):
        raw = name.encode()
        pool.extend(raw)
        return len(pool) - len(raw), len(raw)

    def add_file(content):
        codec, payload = 0, content
        z = zstd_compress(content) if use_zstd and content else None
        if z is not None and len(z) < len(content):
            codec, payload = 1, z
        first = len(refs)
        if content:
            data.extend(b'\0' * (-(8192 + len(data)) % 16))
            blocks.append((8192 + len(data), len(payload), len(content), codec, len(checks) // 4))
            data.extend(payload)
            checks.extend(struct.pack('<I', nx.crc32c(content)))
            refs.append(len(blocks) - 1)
        files.append((len(content), first, 1 if content else 0, S(content).digest()))
        return len(files) - 1

    def add_dir(tree):
        index = len(dirs)
        dirs.append(None)
        kids = [(name, add_dir(v) if isinstance(v, dict) else add_file(v), isinstance(v, dict))
                for name, v in sorted(tree.items(), key=lambda kv: kv[0].encode())]
        first = len(entries)
        for name, target, is_dir in kids:
            entries.append((name_ref(name), target, 1 if is_dir else 2))
        dirs[index] = (first, len(kids))
        return index

    roots = [add_dir(tree) for *_, tree in snapshots]
    strings, snaps = bytearray(), bytearray()
    for (kind, version, flags, content_id, _), root in zip(snapshots, roots):
        text = f'v{version}'.encode()
        strings.extend(text)
        snaps += struct.pack('<IBBHIIQII16sII32s32s', 0xFFFFFFFF, kind, flags, 0, version, root, 0,
                             len(strings) - len(text), len(text), content_id, 0, 0, b'\1' * 32, b'\0' * 32)
    app = struct.pack('<QBBHIIII', APP, 2, 1, 0, 0xFFFFFFFF, 0, 0, len(snapshots)) + b'\0' * 48
    snap_off = 128 + len(app)
    cat = bytearray(struct.pack('<4sHHQ8I8Q16s', b'NXCT', 1, 128, 0, 0, 1, 0, len(snapshots), 0, 0, 0, 0,
                                128, 0, snap_off, 0, 0, 0, snap_off + len(snaps), len(strings), b'\0' * 16))
    cat += app + snaps + strings
    cat[8:16] = struct.pack('<Q', len(cat))
    dir_off = 96
    entry_off = dir_off + 16 * len(dirs)
    file_off = entry_off + 24 * len(entries)
    file_off += -file_off % 8
    ref_off = file_off + 64 * len(files)
    pool_m = ref_off + 4 * len(refs)
    pool_m += -pool_m % 8
    man = bytearray(struct.pack('<4sHHQIIIIQQQQQQ16s', b'NXMF', 1, 96, 0, len(dirs), len(files), len(entries),
                                len(refs), dir_off, file_off, entry_off, ref_off, pool_m, len(pool), b'\0' * 16))
    for first, n in dirs:
        man += struct.pack('<IIII', first, n, 0, 0)
    for (off, ln), target, kind in entries:
        man += struct.pack('<QIIBBHI', off, ln, target, kind, 0, 0, 0)
    man += b'\0' * (file_off - len(man))
    for size, first, n, sha in files:
        man += struct.pack('<QIIBBH32s12s', size, first, n, 16, 0, 0, sha, b'\0' * 12)
    for r in refs:
        man += struct.pack('<I', r)
    man += b'\0' * (pool_m - len(man)) + bytes(pool)
    man[8:16] = struct.pack('<Q', len(man))
    index = b''.join(struct.pack('<QIIBBHI', b[0], b[1], b[2], b[3], 0, 0, b[4]) for b in blocks)
    cat_o = 8192 + len(data)
    cat_o += -cat_o % 16
    man_o = cat_o + len(cat)
    man_o += -man_o % 16
    idx_o = man_o + len(man)
    idx_o += -idx_o % 16
    chk_o = idx_o + len(index)
    chk_o += -chk_o % 16
    head = struct.pack('<4sHHIHHQQQQQQQQQQQQQ32s32s32sQI24s', b'NXAR', 0, 2, 256, 1, 0, chk_o + len(checks), cat_o,
                       len(cat), 0, 0, 8192, len(data), man_o, len(man), idx_o, len(blocks), chk_o, len(checks),
                       S(bytes(cat)).digest(), S(bytes(man)).digest(), S(index + bytes(checks)).digest(), 0, 16384,
                       b'\0' * 24)
    head += struct.pack('<I', nx.crc32c(head))
    out = bytearray(head) + b'\0' * (4096 - 256) + head + b'\0' * (4096 - 256) + data
    for off, blob in ((cat_o, cat), (man_o, man), (idx_o, index), (chk_o, checks)):
        out += b'\0' * (off - len(out)) + blob
    return bytes(out)


NONE = b'\0' * 16
BASE = (0, 1, 0, NONE, {'exefs': {'main': b'm1', 'sdk': b'x'}, 'romfs': {'a.bin': b'A' * 3000, 'd': {'k': b'old'}}})
UPDATE = (1, 2, 0, NONE, {'exefs': {'main': b'm2'}, 'romfs': {'a.bin': b'B' * 40000, 'd': {'k': b'new'}}})
ADDON = (2, 1, 2, struct.pack('<Q', APP + 0x1000) + b'\0' * 8, {'romfs': {'x': b'dlc'}})


class NxTest(unittest.TestCase):
    def open(self, snapshots, zstd=False):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = Path(tmp.name) / 'g.nx'
        path.write_bytes(build(snapshots, zstd))
        c = nup.open_container(path)
        self.addCleanup(c.close)
        return c, Path(tmp.name)

    def test_plan_and_extract(self):
        c, tmp = self.open([BASE, UPDATE, ADDON], zstd=nup.Zstd().kind is not None)
        self.assertEqual([p.kind for p in c.packages], [0, 1, 2])
        self.assertEqual(c.packages[0].display_version, 'v1')
        plan = nup.install_plan(c, 'latest')
        self.assertIs(plan['exefs'], c.packages[1])
        z = nup.Zstd()
        with contextlib.redirect_stdout(io.StringIO()):
            nup.extract_tree(c, z, None, 'exefs', tmp / 'exefs', True, True,
                             nup.merged_entries(c, [plan['exefs']], 'exefs'))
            nup.pack_romfs(c, z, nup.merged_entries(c, plan['romfs'], 'romfs'), tmp / 'r', True, True)
            self.assertEqual(nup.write_aoc(c, z, tmp / 'aoc', True, True), 1)
        self.assertEqual((tmp / 'exefs' / 'main').read_bytes(), b'm2')
        self.assertFalse((tmp / 'exefs' / 'sdk').exists())   # not in the update's tree
        index = json.loads((tmp / 'r' / 'index.json').read_text())
        self.assertEqual([f['path'] for f in index['files']], ['a.bin', 'd/k'])
        image = (tmp / 'r' / 'romfs.bin').read_bytes()
        self.assertEqual(image[:40000], b'B' * 40000)
        self.assertEqual(image[index['files'][1]['offset']:][:3], b'new')
        self.assertEqual((tmp / 'aoc' / 'aoc-0' / 'romfs.bin').read_bytes(), b'dlc')

    def test_corrupt(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        raw = bytearray(build([BASE], False))
        path = Path(tmp.name) / 'g.nx'
        raw[8192] ^= 1   # the first data byte: the file SHA-256 catches it
        path.write_bytes(raw)
        c = nup.open_container(path)
        self.addCleanup(c.close)
        entries = nup.merged_entries(c, [c.packages[0]], 'exefs')
        with self.assertRaises(nup.NupError):
            nup.extract_tree(c, nup.Zstd(), None, 'exefs', Path(tmp.name) / 'o', True, True, entries)
        raw[0:4] = b'XXXX'
        raw[4096:4100] = b'XXXX'
        path.write_bytes(raw)
        with self.assertRaises(nup.NupError):
            nup.open_container(path)


if __name__ == '__main__':
    unittest.main()
