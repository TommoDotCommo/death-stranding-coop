#!/usr/bin/env python3
"""Build the test save pack, deathstranding-coop-test-saves-<version>.zip, a separate download beside each release.

    python3 tools/make_test_saves.py SRC_DIR OUT.zip --version V

SRC_DIR (kept outside the repository: saves never go into git) holds sets.json and one folder per set:

    sets.json                    {"sets": [{"id": "ep3-28h", "title": "...", "description": "...", "episode": 3,
                                            "play_time": "28 h", "source": "who made it", "continue": "manualsave0"}]}
    ep3-28h/manualsave0/checkpoint.dat
    ep3-28h/autosave1/checkpoint.dat

Only checkpoint folders (autosaveN, manualsaveN, quicksaveN) holding a checkpoint.dat that is a Death Stranding
checkpoint are taken. Anything else (profile.dat, another file, a link, a folder not named in sets.json) is refused and
nothing is written. Every checkpoint is re-signed for a placeholder account so the pack names nobody's Steam account;
the launcher re-signs it for the player when it is used. `continue` (optional) is the folder Continue should load; by
default the newest checkpoint. The zip stores its entries (no compression: the saves are encrypted) in a fixed order
with fixed times, so the same input makes the same bytes. The format is launcher/testsaves.h.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

SCHEMA = 1
MAX_SAVE = 16 << 20
PLACEHOLDER = 'steam_76561197960265728'   # account 0: nobody's; the launcher re-signs for the player
MAGIC = 0xC0391E1D
KEY_MATERIAL = bytes([0x46, 0x3F, 0xBB, 0xBC, 0x7B, 0x6F, 0x57, 0xED, 0x9B, 0x41, 0x7D, 0x36, 0xE3, 0x26, 0x12, 0xBC])
SAVE_FOLDER = re.compile(r'^(autosave|manualsave|quicksave)[0-9]{1,4}$', re.I)
SET_ID = re.compile(r'^[a-z0-9][a-z0-9._-]{0,47}$')
TITLE_MAX, DESCRIPTION_MAX = 160, 2000
M64 = (1 << 64) - 1


class Refused(Exception):
    """The input is not a pack's: nothing was written."""


# ---- the save container (launcher/saves.cpp; after mi5hmash/DeathStrandingSaveDataResigner, MIT) ----
def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M64


def _fmix(k):
    k ^= k >> 33; k = (k * 0xFF51AFD7ED558CCD) & M64; k ^= k >> 33; k = (k * 0xC4CEB9FE1A85EC53) & M64; k ^= k >> 33
    return k


def _key(salt):
    """MurmurHash3 x64-128 (seed 0x2A) over the block's four salt bytes and twelve fixed ones."""
    k0, k1 = struct.unpack('<QQ', salt + KEY_MATERIAL[4:])
    s, c1, c2 = 0x2A, 0x87C37B91114253D5, 0x4CF5AD432745937F
    k0 = (k0 * c1) & M64; k0 = _rotl(k0, 31); k0 = (k0 * c2) & M64; k0 ^= s; k0 = _rotl(k0, 27); k0 = (k0 + s) & M64
    k0 = (k0 * 5 + 0x52DCE729) & M64
    k1 = (k1 * c2) & M64; k1 = _rotl(k1, 33); k1 = (k1 * c1) & M64; k1 ^= s; k1 = _rotl(k1, 31); k1 = (k1 + k0) & M64
    k1 = (k1 * 5 + 0x38495AB5) & M64
    k0 ^= 16; k1 ^= 16; k0 = (k0 + k1) & M64; k1 = (k1 + k0) & M64; k0 = _fmix(k0); k1 = _fmix(k1)
    k0 = (k0 + k1) & M64; k1 = (k1 + k0) & M64
    return struct.pack('<QQ', k0, k1)


def _cipher(block):
    """Toggles the encryption of a block that starts at the magic. The tail shorter than eight bytes restarts the key."""
    if len(block) < 12:
        raise Refused('not a Death Stranding save')
    key = _key(bytes(block[4:8]))
    out = bytearray(block)
    n = len(block) - 8
    full = n - n % 8
    for i in range(full):
        out[8 + i] ^= key[i % 16]
    for i in range(full, n):
        out[8 + i] ^= key[i - full]
    return out


def parse_checkpoint(data):
    """(meta, picture, decrypted block) of a checkpoint; Refused for anything else (a profile, junk)."""
    if len(data) > MAX_SAVE or len(data) < 24 + 12 or struct.unpack_from('<I', data, 0)[0] == MAGIC:
        raise Refused('not a checkpoint')
    sizes = []
    for i in range(3):
        field = data[i * 8:i * 8 + 8]
        digits = field.split(b'\0', 1)[0]
        if not digits or not digits.isdigit():
            raise Refused('not a checkpoint')
        sizes.append(int(digits))
    meta, picture, block_size = sizes
    if 24 + meta + picture + 12 > len(data):
        raise Refused('not a checkpoint')
    block = data[24 + meta + picture:]
    if struct.unpack_from('<I', block, 0)[0] != MAGIC or block_size != len(block):
        raise Refused('not a checkpoint')
    block = _cipher(block)
    n = struct.unpack_from('<I', block, 8)[0]
    if n > 255 or 12 + n > len(block) or any(c < 0x21 or c > 0x7e for c in block[12:12 + n]):
        raise Refused('not a checkpoint')
    return data[24:24 + meta], data[24 + meta:24 + meta + picture], bytes(block)


def account_of(data):
    _meta, _picture, block = parse_checkpoint(data)
    n = struct.unpack_from('<I', block, 8)[0]
    return block[12:12 + n].decode('ascii')


def resign(data, user):
    """The checkpoint signed for `user` ("steam_<64-bit id>"); its size fields written clean."""
    meta, picture, block = parse_checkpoint(data)
    n = struct.unpack_from('<I', block, 8)[0]
    user = user.encode('ascii')
    block = block[:8] + struct.pack('<I', len(user)) + user + block[12 + n:]
    block = bytes(_cipher(block))
    head = b''.join(str(v).encode().ljust(8, b'\0') for v in (len(meta), len(picture), len(block)))
    return head + meta + picture + block


# ---- the pack ----
def _text(value, limit, lines=False):
    if value is None:
        return ''
    if not isinstance(value, str):
        raise Refused('sets.json: text fields must be strings')
    value = ''.join(c if (c >= ' ' and c != '\x7f') or (lines and c == '\n') else ' ' for c in value)
    if len(value.encode('utf-8')) > limit:
        raise Refused(f'sets.json: a text is longer than {limit} bytes: {value[:40]!r}...')
    return value


def _regular(path):
    st = os.lstat(path)
    return stat.S_ISREG(st.st_mode)


def collect(source):
    """The manifest's sets with their files' bytes, from SRC_DIR; Refused on anything that is not a checkpoint."""
    source = Path(source)
    sets_file = source / 'sets.json'
    if not sets_file.is_file() or sets_file.is_symlink():
        raise Refused(f'{source}: sets.json is missing')
    try:
        listed = json.loads(sets_file.read_text(encoding='utf-8'))
    except (ValueError, UnicodeDecodeError) as e:
        raise Refused(f'sets.json is not readable: {e}')
    if isinstance(listed, dict):
        listed = listed.get('sets')
    if not isinstance(listed, list) or not listed:
        raise Refused('sets.json lists no sets')
    ids = [s.get('id') if isinstance(s, dict) else None for s in listed]
    for entry in sorted(os.listdir(source)):
        if entry == 'sets.json':
            continue
        if entry not in ids:
            raise Refused(f'{entry}: not a set named in sets.json')
    out = []
    for s in listed:
        if not isinstance(s, dict) or not isinstance(s.get('id'), str) or not SET_ID.match(s['id']):
            raise Refused('sets.json: every set needs an id of lower-case letters, digits, ".", "-" or "_"')
        sid = s['id']
        if any(o['id'] == sid for o in out):
            raise Refused(f'sets.json: {sid} twice')
        title = _text(s.get('title'), TITLE_MAX).strip()
        if not title:
            raise Refused(f'sets.json: {sid} has no title')
        episode = s.get('episode')
        if episode is not None and (not isinstance(episode, int) or isinstance(episode, bool) or not 0 <= episode <= 99):
            raise Refused(f'sets.json: {sid} episode must be a number')
        play_time = s.get('play_time')
        if isinstance(play_time, (int, float)) and not isinstance(play_time, bool):
            play_time = f'{play_time} h'
        folder = source / sid
        if folder.is_symlink() or not folder.is_dir():
            raise Refused(f'{sid}: its folder is missing')
        files = []
        for entry in sorted(os.listdir(folder)):
            path = folder / entry
            if path.is_symlink() or not path.is_dir() or not SAVE_FOLDER.match(entry):
                raise Refused(f'{sid}/{entry}: not a checkpoint folder (autosaveN, manualsaveN, quicksaveN)')
            inside = os.listdir(path)
            if inside != ['checkpoint.dat'] or not _regular(path / 'checkpoint.dat'):
                raise Refused(f'{sid}/{entry}: a checkpoint folder holds checkpoint.dat and nothing else (found {sorted(inside)})')
            data = (path / 'checkpoint.dat').read_bytes()
            try:
                data = resign(data, PLACEHOLDER)
            except Refused:
                raise Refused(f'{sid}/{entry}/checkpoint.dat: not a Death Stranding checkpoint')
            name = entry.lower()
            if any(f['folder'] == name for f in files):
                raise Refused(f'{sid}/{entry}: two folders of that name')
            files.append(dict(folder=name, data=data, mtime=(path / 'checkpoint.dat').stat().st_mtime))
        if not files:
            raise Refused(f'{sid}: no checkpoint folders')
        first = s.get('continue')
        if first is None:
            first = max(files, key=lambda f: (f['mtime'], f['folder']))['folder']
        if not isinstance(first, str) or first.lower() not in [f['folder'] for f in files]:
            raise Refused(f'sets.json: {sid} continue names no folder of the set')
        files.sort(key=lambda f: (f['folder'] != first.lower(), f['folder']))
        out.append(dict(id=sid, title=title, description=_text(s.get('description'), DESCRIPTION_MAX, True),
                        episode=episode, play_time=_text(play_time, 32), source=_text(s.get('source', s.get('credit')), 300),
                        files=files))
    return out


def manifest(version, sets):
    return dict(schema=SCHEMA, version=version, sets=[
        dict(id=s['id'], title=s['title'], description=s['description'], episode=s['episode'], play_time=s['play_time'],
             source=s['source'],
             files=[dict(path=f"{s['id']}/{f['folder']}/checkpoint.dat", sha256=hashlib.sha256(f['data']).hexdigest(),
                         size=len(f['data'])) for f in s['files']])
        for s in sets])


def build(source, out, version):
    if not version or len(version) > 64 or not version.isprintable():
        raise Refused('--version is required (the release version)')
    sets = collect(source)
    listing = json.dumps(manifest(version, sets), indent=1, ensure_ascii=False).encode('utf-8') + b'\n'
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=out.parent, prefix='.test-saves-', suffix='.zip')
    os.close(handle)
    try:
        with zipfile.ZipFile(temp, 'w', zipfile.ZIP_STORED) as z:
            def put(name, data):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = 0o644 << 16
                z.writestr(info, data)
            put('test-saves.json', listing)
            for s in sets:
                for f in s['files']:
                    put(f"{s['id']}/{f['folder']}/checkpoint.dat", f['data'])
        os.replace(temp, out)
    except BaseException:
        os.unlink(temp)
        raise
    return sets


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('source', help='folder with sets.json and one folder of checkpoint folders per set')
    parser.add_argument('out', help='the zip to write, deathstranding-coop-test-saves-<version>.zip')
    parser.add_argument('--version', required=True, help='the release version the pack goes with')
    args = parser.parse_args(argv)
    try:
        sets = build(args.source, args.out, args.version)
    except Refused as e:
        print(f'make_test_saves: refused: {e}', file=sys.stderr)
        return 2
    for s in sets:
        print(f"{s['id']}\t{len(s['files'])} checkpoint(s)\t{s['title']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
