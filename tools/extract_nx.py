"""Reader for NX archives (.nx, magic NXAR), the NXFormat container https://github.com/fab918/nxformat (docs/SPEC.md,
MIT). It gives the same view of a container as tools/extract_nup.py's Nup, so the installer and extract_nup.py's
extraction, merging and packing code run on a .nx unchanged: each snapshot of the first application becomes a package
(base, update or add-on content) whose root folder holds `exefs/` and `romfs/`.

Not a command line tool: extract_nup.py calls this for a file that starts with NXAR (open_container). Written from the
specification, not from the reference reader. Checked: the header CRC-32C of both slots, the SHA-256 of the catalog,
the manifest and the block index, and (unless --no-verify) the SHA-256 of every file it hands out. Not checked: the
per-16-KiB CRC-32C slices, which the file SHA-256 covers. NX 0.3 source resources are ignored. Blocks are RAW or zstd,
as in the specification; there is no encryption.

A snapshot is a complete tree, so an update's RomFS already holds every file; laying it over the base (as for a .nup)
gives the same files plus any the update deleted.
"""
import hashlib
import struct
from pathlib import Path

NO_DIR = 0xFFFFFFFF
MAGIC = b'NXAR'
HEADER = struct.Struct('<4sHHIHHQQQQQQQQQQQQQ32s32s32sQI24sI')
CAT_HEADER = struct.Struct('<4sHHQ8I8Q16s')
MAN_HEADER = struct.Struct('<4sHHQIIIIQQQQQQ16s')
APP = struct.Struct('<QBBHIIII')    # application_id, platform, flags, _, first/count program, first/count snapshot
SNAP = struct.Struct('<IBBHIIQII16sII32s32s')
SNAP_SIZE = 120
DIR = struct.Struct('<IIII')
ENTRY = struct.Struct('<QIIBBHI')
FILE = struct.Struct('<QIIBBH32s12s')
BLOCK = struct.Struct('<QIIBBHI')
CODEC_RAW, CODEC_ZSTD = 0, 1
SNAP_KINDS = {0: 0, 1: 1, 2: 2, 3: 1}   # base, update, DLC, standalone update -> extract_nup package kinds
ADDON_TITLE_ID = 2

_CRC = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ (0x82F63B78 if _c & 1 else 0)
    _CRC.append(_c)


def crc32c(data):
    c = 0xFFFFFFFF
    for b in data:
        c = _CRC[(c ^ b) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


class _Files:
    """container.files[idx] as extract_nup has it: (name, size, extent offset, extent size, sha256, codec). The name is
    empty (an NX file has no name of its own) and the extent unused; codec is 1 when any of its blocks is zstd."""

    def __init__(self, nx):
        self.nx = nx

    def __len__(self):
        return self.nx.file_count

    def __getitem__(self, idx):
        nx = self.nx
        size, first, count, sha = nx.file_record(idx)
        codec = max((nx.block(nx.block_id(ref))[3] for ref in range(first, first + count)), default=CODEC_RAW)
        return '', size, 0, 0, sha, codec


class Nx:
    def __init__(self, path, nup):
        """`nup` is the extract_nup module of the caller, whose NupError, Package and safe_name this uses, so errors
        and packages are the ones the caller handles."""
        self.nup = nup
        self.path = Path(path)
        self.f = open(self.path, 'rb')
        try:
            self._open()
        except BaseException:
            self.f.close()
            raise

    def error(self, text):
        return self.nup.NupError('NX: ' + text)

    def close(self):
        self.f.close()

    def _read(self, off, size, what):
        if off + size > self.file_size:
            raise self.error(f'{what} lies outside the file')
        self.f.seek(off)
        data = self.f.read(size)
        if len(data) != size:
            raise self.error(f'{what} is truncated')
        return data

    def _header(self):
        """The valid header slot with the highest generation."""
        best = None
        for slot in (0, 4096):
            self.f.seek(slot)
            raw = self.f.read(256)
            if len(raw) != 256 or raw[:4] != MAGIC or crc32c(raw[:252]) != struct.unpack_from('<I', raw, 252)[0]:
                continue
            h = HEADER.unpack(raw)
            if h[1] != 0 or h[2] not in (2, 3) or h[3] != 256 or h[4] != 1 or h[5] != 0:
                raise self.error(f'unsupported NX version {h[1]}.{h[2]} or features {h[4]:#x}/{h[5]:#x}')
            if best is None or h[22] > best[22]:
                best = h
        if best is None:
            raise self.error('not an NX archive (no valid header slot)')
        return best

    def _open(self):
        self.f.seek(0, 2)
        self.file_size = self.f.tell()
        (_, _, _, _, _, _, archive_size, cat_off, cat_size, _, _, _, _, man_off, man_size, idx_off, self.block_count,
         chk_off, chk_size, cat_sha, man_sha, idx_sha, _, _, _, _) = self._header()
        if archive_size > self.file_size:
            raise self.error('the archive is truncated')
        cat = self._read(cat_off, cat_size, 'catalog')
        man = self._read(man_off, man_size, 'manifest')
        index = check = b''
        if self.block_count:
            index = self._read(idx_off, self.block_count * 24, 'block index')
            check = self._read(chk_off, chk_size, 'integrity table')
        for what, data, sha in (('catalog', cat, cat_sha), ('manifest', man, man_sha),
                                ('block index', index + check, idx_sha)):
            if hashlib.sha256(data).digest() != sha:
                raise self.error(f'{what} SHA-256 mismatch: the file is corrupt or was modified')
        self.index = index
        self._manifest(man)
        self._catalog(cat)
        self.files = _Files(self)

    def _manifest(self, man):
        if len(man) < 96 or man[:4] != b'NXMF':
            raise self.error('bad manifest')
        (_, _, _, _, self.dir_count, self.file_count, self.entry_count, self.ref_count, self.dir_off, self.file_off,
         self.entry_off, self.ref_off, pool_off, pool_size, _) = MAN_HEADER.unpack_from(man)
        for off, n, size in ((self.dir_off, self.dir_count, 16), (self.file_off, self.file_count, 64),
                             (self.entry_off, self.entry_count, 24), (self.ref_off, self.ref_count, 4)):
            if n and off + n * size > len(man):
                raise self.error('manifest table outside the manifest')
        if pool_off + pool_size > len(man):
            raise self.error('manifest path pool outside the manifest')
        self.man = man
        self.pool = man[pool_off:pool_off + pool_size]

    def _catalog(self, cat):
        if len(cat) < 128 or cat[:4] != b'NXCT':
            raise self.error('bad catalog')
        (_, _, _, _, _, apps, _, _, _, _, _, _, app_off, _, snap_off, _, _, _, pool_off, pool_size,
         _) = CAT_HEADER.unpack_from(cat)
        if not apps or pool_off + pool_size > len(cat) or app_off + APP.size > len(cat):
            raise self.error('bad catalog')
        pool = cat[pool_off:pool_off + pool_size]
        # The first application: a dump holds one game. Its snapshots are the packages.
        app_id, _, _, _, _, _, first, count = APP.unpack_from(cat, app_off)
        self.packages = []
        for i in range(first, first + count):
            if snap_off + (i + 1) * SNAP_SIZE > len(cat):
                raise self.error('snapshot table outside the catalog')
            (_, kind, flags, _, version, root, _, ver_off, ver_len, content_id, _, _, _, _) = \
                SNAP.unpack_from(cat, snap_off + i * SNAP_SIZE)
            if kind not in SNAP_KINDS:
                continue
            title = app_id
            if SNAP_KINDS[kind] == 2:   # add-on content names its title id only with the ADDON_TITLE_ID flag
                if not flags & ADDON_TITLE_ID:
                    continue
                title = struct.unpack_from('<Q', content_id)[0]
            pkg = self.nup.Package(len(self.packages), (title, root, self._child(root, 'exefs'),
                                                        self._child(root, 'romfs'), version, SNAP_KINDS[kind],
                                                        1 if kind == 3 else 0, 0, 0, 0, 0))
            if ver_off != 0xFFFFFFFF:
                pkg.display_version = pool[ver_off:ver_off + ver_len].decode('utf-8', 'replace')
            self.packages.append(pkg)
        if not self.packages:
            raise self.error('no usable snapshot in the archive')

    def file_record(self, idx):
        """(size, first block reference, block count, SHA-256) of a file."""
        if idx >= self.file_count:
            raise self.error('file index out of range')
        size, first, count, _, _, _, sha, _ = FILE.unpack_from(self.man, self.file_off + idx * 64)
        if first + count > self.ref_count:
            raise self.error('file block references outside the table')
        return size, first, count, sha

    def block_id(self, ref):
        return struct.unpack_from('<I', self.man, self.ref_off + ref * 4)[0]

    def block(self, block_id):
        """(offset, stored size, logical size, codec) of a block."""
        if block_id >= self.block_count:
            raise self.error('block index out of range')
        off, stored, logical, codec, _, _, _ = BLOCK.unpack_from(self.index, block_id * 24)
        return off, stored, logical, codec

    def entries(self, dir_id):
        """(name, kind, target) of the children of a directory; kind 1 is a directory, 2 a file."""
        if dir_id >= self.dir_count:
            raise self.error('directory index out of range')
        first, count, _, _ = DIR.unpack_from(self.man, self.dir_off + dir_id * 16)
        if first + count > self.entry_count:
            raise self.error('directory entries outside the entry table')
        for i in range(first, first + count):
            name_off, name_len, target, kind, _, _, _ = ENTRY.unpack_from(self.man, self.entry_off + i * 24)
            if name_off + name_len > len(self.pool):
                raise self.error('name outside the path pool')
            yield self.pool[name_off:name_off + name_len].decode('utf-8'), kind, target

    def _child(self, dir_id, name):
        for n, kind, target in self.entries(dir_id):
            if n == name and kind == 1:
                return target
        return NO_DIR

    def walk(self, dir_index, prefix=(), path=()):
        if dir_index in path:
            raise self.error('directory cycle')
        for name, kind, target in self.entries(dir_index):
            parts = prefix + (self.nup.safe_name(name),)
            if kind == 2:
                yield parts, target
            else:
                yield from self.walk(target, parts, path + (dir_index,))

    def package_files(self, pkg, which):
        dir_index = pkg.exefs_dir if which == 'exefs' else pkg.romfs_dir
        return [] if dir_index == NO_DIR else list(self.walk(dir_index))

    def file_chunks(self, zstd, idx, verify):
        """extract_nup.file_chunks for an NX file: its blocks in order, then the size and SHA-256 checks."""
        size, first, count, sha = self.file_record(idx)
        h = hashlib.sha256()
        written = 0
        for ref in range(first, first + count):
            off, stored, logical, codec = self.block(self.block_id(ref))
            if codec == CODEC_RAW:
                data = self._read(off, stored, 'block')
            else:
                self.f.seek(off)
                data = b''.join(zstd.stream(self.f, stored))
            if len(data) != logical:
                raise self.error(f'file {idx}: a block decodes to {len(data)} bytes, not {logical}')
            h.update(data)
            written += len(data)
            yield data
        if written != size:
            raise self.error(f'file {idx}: expected {size} bytes, got {written}')
        if verify and h.digest() != sha:
            raise self.error(f'file {idx}: SHA-256 differs from the archive record (corrupt archive)')
