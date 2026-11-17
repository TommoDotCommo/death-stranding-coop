#!/usr/bin/env python3
"""Game-free check of a built release (tools/release.sh runs it last): checksums, the exact inventory, Windows PE headers
and VERSIONINFO with the release version, the Setup scripts equal to dist/, coop-mod.json against the zip (with the
pinned ds.exe build), and the single-file Setup whose embedded payload passes its SHA-256 and equals the zip.

  check_release_package.py <zip>     the zip and DeathStrandingCoop-Setup.exe beside it (with their .sha256 files)
"""
import hashlib
from pathlib import Path
import struct
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mod_manifest  # noqa: E402
import package  # noqa: E402
import payload  # noqa: E402
import versioninfo  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def check_sha256(path):
    digest, name = path.with_name(path.name + '.sha256').read_text().split()
    assert name == path.name, 'the checksum file names another asset: ' + path.name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'checksum mismatch: ' + path.name


def check_pe(data, version, name):
    assert data[:2] == b'MZ', '%s is not a Windows PE' % name
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0', '%s has no PE header' % name
    assert struct.unpack_from('<H', data, pe + 4)[0] == 0x8664, '%s is not 64-bit' % name
    assert versioninfo.read(data, 'ProductName') == versioninfo.PRODUCT, 'ProductName of %s is not %s' % (name, versioninfo.PRODUCT)
    assert versioninfo.read(data) == version, 'ProductVersion of %s is not %s' % (name, version)


def check(archive):
    archive = Path(archive)
    assert archive.name.startswith('deathstranding-coop-') and archive.name.endswith('.zip'), 'not a release zip name'
    version = archive.name[len('deathstranding-coop-'):-len('.zip')]
    top = archive.name[:-len('.zip')] + '/'
    check_sha256(archive)
    with zipfile.ZipFile(archive) as z:
        assert sorted(z.namelist()) == sorted(top + f for f in package.EXPECTED), 'unexpected release inventory: %s' % sorted(z.namelist())
        members = {f: z.read(top + f) for f in package.EXPECTED}
    for name in package.SHIPPED_PE:
        check_pe(members[name], version, name)
    for name in package.SCRIPTS:
        assert members[name] == package.crlf((ROOT / 'dist' / name).read_text(encoding='utf-8')).encode('utf-8'), 'stale Setup script: ' + name
    assert ('Death Stranding Co-op %s' % version).encode() in members['README.txt'], 'README.txt does not name the version'
    assert payload.read_exe_bytes(members[package.SETUP]) is None, 'the zip\'s Setup must not carry a payload (it uses the files beside it)'
    mod_manifest.check(archive)
    setup = archive.with_name(package.SETUP)
    check_sha256(setup)
    data = setup.read_bytes()
    check_pe(data, version, 'single-file Setup')
    got = payload.read_exe_bytes(data)
    assert got, 'the single-file Setup carries no payload'
    embedded_version, want, blob = got
    assert hashlib.sha256(blob).hexdigest() == want, 'the embedded payload does not match its SHA-256'
    assert embedded_version == version, 'the embedded version differs from the zip'
    files = dict(payload.unpack(blob))
    assert sorted(files) == package.PAYLOAD, 'the embedded payload inventory differs: %s' % sorted(files)
    for name in package.PAYLOAD:
        assert files[name] == members[name], 'the embedded payload differs from the zip: ' + name
    return ('PASS: checksums, exact inventory, 64-bit PE files with VERSIONINFO %s, Setup scripts as in dist/, coop-mod.json '
            'against the zip and the pinned ds.exe build, and the single-file Setup whose verified payload equals the zip' % version)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    try:
        print(check(sys.argv[1]))
    except (AssertionError, KeyError, OSError, ValueError) as e:
        print('FAIL: %s' % e, file=sys.stderr)
        sys.exit(1)
