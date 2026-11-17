#!/usr/bin/env python3
"""tools/release.sh end to end on stand-in binaries (RELEASE_PREBUILT, so no `make all`): the zip, the single-file Setup
with its verified payload, coop-mod.json, VERSIONINFO, the notes; then tampered copies that tools/check_release_package.py
must refuse. Needs the MinGW cross-compiler (skipped, and said so, without it)."""
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import check_release_package  # noqa: E402
import payload  # noqa: E402
import versioninfo  # noqa: E402

VERSION = 'v0.0.0-test.1'
CC, WINDRES = 'x86_64-w64-mingw32-gcc', 'x86_64-w64-mingw32-windres'


def stand_in(folder, name):
    """A small 64-bit PE (exe or dll) with the release's VERSIONINFO."""
    rc, res, src = folder / (name + '.rc'), folder / (name + '.o'), folder / (name + '.c')
    rc.write_text(versioninfo.script(VERSION, name, 'stand-in ' + name))
    subprocess.run([WINDRES, str(rc), '-o', str(res)], check=True)
    src.write_text('int stand_in_%s(void) { return 0; }\n' % re.sub(r'\W', '_', name) + ('' if name.endswith('.dll') else 'int main(void) { return stand_in_%s(); }\n' % re.sub(r'\W', '_', name)))
    subprocess.run([CC, '-O1'] + (['-shared'] if name.endswith('.dll') else []) + [str(src), str(res), '-o', str(folder / name)], check=True)


def refused(out, what):
    try:
        check_release_package.check(out / ('deathstranding-coop-%s.zip' % VERSION))
    except (AssertionError, KeyError, ValueError) as e:
        return str(e)
    raise SystemExit('release oracle: a tampered release passed: ' + what)


def rezip(zip_path, change):
    """Rewrite the zip with change(name, bytes) -> bytes or None (drop), and its checksum."""
    with zipfile.ZipFile(zip_path) as z:
        members = [(i, z.read(i.filename)) for i in z.infolist()]
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for info, data in members:
            data = change(info.filename, data)
            if data is not None:
                z.writestr(info, data)
    zip_path.with_name(zip_path.name + '.sha256').write_text('%s  %s\n' % (hashlib.sha256(zip_path.read_bytes()).hexdigest(), zip_path.name))


def main():
    if not shutil.which(CC) or not shutil.which(WINDRES):
        print('SKIPPED: tools/test_release.py needs %s and %s' % (CC, WINDRES))
        return
    with tempfile.TemporaryDirectory(prefix='ds-release-') as tmp:
        tmp = Path(tmp)
        bins = tmp / 'bin'
        bins.mkdir()
        for name in ('deathstranding-coop.exe', 'coop-world.exe', 'avrt.dll', 'mod.dll'):
            stand_in(bins, name)
        good = tmp / 'good'
        r = subprocess.run(['bash', str(HERE / 'release.sh'), VERSION], capture_output=True, text=True,
                           env={**__import__('os').environ, 'RELEASE_PREBUILT': str(bins), 'RELEASE_OUT': str(good)})
        if r.returncode:
            raise SystemExit('release oracle: release.sh failed\n' + r.stdout + r.stderr)
        assert 'PASS:' in r.stdout, 'release.sh ran the package check'
        name = 'deathstranding-coop-%s' % VERSION
        for f in ('DeathStrandingCoop-Setup.exe', 'DeathStrandingCoop-Setup.exe.sha256', name + '.zip', name + '.zip.sha256',
                  'NEXUS-%s.txt' % VERSION, 'PUBLISH-%s.txt' % VERSION):
            assert (good / f).is_file(), 'release output ' + f
        publish = (good / ('PUBLISH-%s.txt' % VERSION)).read_text()
        assert 'gh release create %s' % VERSION in publish and '--prerelease' in publish, 'the publish note holds the gh command'
        assert not (good / ('work-' + VERSION)).exists(), 'no work folder left'

        def variant(label):
            copy = tmp / label
            shutil.copytree(good, copy)
            return copy
        zip_name = name + '.zip'
        v = variant('checksum'); (v / (zip_name + '.sha256')).write_text('0' * 64 + '  ' + zip_name + '\n'); refused(v, 'a wrong zip checksum')
        v = variant('payload'); setup = v / 'DeathStrandingCoop-Setup.exe'; data = bytearray(setup.read_bytes())
        at = data.find(payload.read_exe_bytes(bytes(data))[2]) + 40; data[at] ^= 0xff; setup.write_bytes(bytes(data))
        (v / 'DeathStrandingCoop-Setup.exe.sha256').write_text('%s  DeathStrandingCoop-Setup.exe\n' % hashlib.sha256(bytes(data)).hexdigest())
        why = refused(v, 'a damaged embedded payload'); assert 'SHA-256' in why, why
        v = variant('script'); rezip(v / zip_name, lambda n, d: d + b'\r\n# changed\r\n' if n.endswith('install-state.ps1') else d); refused(v, 'a stale Setup script')
        v = variant('manifest'); rezip(v / zip_name, lambda n, d: d.replace(b'"size": ', b'"size": 1', 1) if n.endswith('coop-mod.json') else d); refused(v, 'a manifest that does not match')
        v = variant('extra')
        with zipfile.ZipFile(v / zip_name, 'a') as z: z.writestr(name + '/extra.dll', b'MZ')
        (v / (zip_name + '.sha256')).write_text('%s  %s\n' % (hashlib.sha256((v / zip_name).read_bytes()).hexdigest(), zip_name))
        refused(v, 'an extra file in the zip')
        v = variant('proxy'); rezip(v / zip_name, lambda n, d: d + b'\0' if n.endswith('/avrt.dll') else d); refused(v, 'a proxy that differs from the Setup payload and the manifest')
    print('PASS: release.sh on stand-in binaries (zip, single-file Setup, coop-mod.json, VERSIONINFO, notes); the check refuses '
          'a wrong checksum, a damaged payload, a stale script, a wrong manifest, an extra file and a changed proxy')


if __name__ == '__main__':
    main()
