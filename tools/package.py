#!/usr/bin/env python3
"""The release's file layout, zip and notes (tools/release.sh compiles, this assembles; tools/test_release.py runs the same
code on stand-in binaries). Standard library only.

  package.py stage <stage dir> <version> <launcher.exe> <coop-world.exe> <avrt.dll> <mod.dll>
        the package folder: the binaries, the Setup scripts (CRLF), README.txt, LICENSE.txt, THIRD-PARTY-NOTICES.txt.
        The zip's payload-less DeathStrandingCoop-Setup.exe and coop-mod.json are added by release.sh afterwards.
  package.py payload-args <stage dir>     prints name=path for every file the single-file Setup embeds
  package.py zip <out dir> <name>         <name>.zip of <out>/<name>/ (deterministic: sorted, SOURCE_DATE_EPOCH) + .sha256
  package.py notes <out dir> <version>    NEXUS-<version>.txt and PUBLISH-<version>.txt (the gh command, never run)
"""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SETUP = 'DeathStrandingCoop-Setup.exe'
SCRIPTS = ['installer-gui.ps1', 'install-state.ps1', 'deathstranding-coop-setup.ps1']
DOCS = ['README.txt', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt']
BINARIES = ['deathstranding-coop.exe', 'coop-world.exe', 'avrt.dll', 'redirect-data/mod/mod.dll']
# Everything in the zip's top folder; the single-file Setup embeds all of it except its own payload-less twin.
EXPECTED = sorted([SETUP] + SCRIPTS + DOCS + BINARIES + ['coop-mod.json'])
PAYLOAD = sorted(set(EXPECTED) - {SETUP})
SHIPPED_PE = [SETUP, 'deathstranding-coop.exe', 'coop-world.exe', 'avrt.dll', 'redirect-data/mod/mod.dll']


def crlf(text):
    return text.replace('\r\n', '\n').replace('\n', '\r\n')


def stage(folder, version, launcher, world, proxy, mod):
    folder = Path(folder)
    if folder.exists():
        shutil.rmtree(folder)
    (folder / 'redirect-data' / 'mod').mkdir(parents=True)
    for source, name in ((launcher, 'deathstranding-coop.exe'), (world, 'coop-world.exe'), (proxy, 'avrt.dll'),
                         (mod, 'redirect-data/mod/mod.dll')):
        shutil.copyfile(source, folder / name)
    for name in SCRIPTS:
        (folder / name).write_bytes(crlf((ROOT / 'dist' / name).read_text(encoding='utf-8')).encode('utf-8'))
    readme = (ROOT / 'dist' / 'README.txt').read_text(encoding='utf-8').replace('@VERSION@', version)
    (folder / 'README.txt').write_bytes(crlf(readme).encode('utf-8'))
    (folder / 'LICENSE.txt').write_bytes(crlf((ROOT / 'LICENSE').read_text(encoding='utf-8')).encode('utf-8'))
    (folder / 'THIRD-PARTY-NOTICES.txt').write_bytes(crlf((ROOT / 'dist' / 'THIRD-PARTY-NOTICES.txt').read_text(encoding='utf-8')).encode('utf-8'))


def payload_args(folder):
    return ['%s=%s' % (name, Path(folder) / name) for name in PAYLOAD]


def make_zip(out, name):
    out = Path(out)
    epoch = max(315532800, int(os.environ.get('SOURCE_DATE_EPOCH', '315532800')))   # ZIP begins in 1980
    stamp = time.gmtime(epoch)[:6]
    top = out / name
    with zipfile.ZipFile(out / (name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(p for p in top.rglob('*') if p.is_file()):
            info = zipfile.ZipInfo((Path(name) / path.relative_to(top)).as_posix(), date_time=stamp)
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            z.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
    write_sha256(out / (name + '.zip'))


def write_sha256(path):
    path = Path(path)
    path.with_name(path.name + '.sha256').write_text('%s  %s\n' % (hashlib.sha256(path.read_bytes()).hexdigest(), path.name))


def notes(out, version):
    out = Path(out)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import mod_manifest
    name = 'deathstranding-coop-%s' % version
    (out / ('NEXUS-%s.txt' % version)).write_text(mod_manifest.nexus_note(out / (name + '.zip')))
    assets = [SETUP, SETUP + '.sha256', name + '.zip', name + '.zip.sha256']
    for pack in sorted(out.glob('deathstranding-coop-test-saves-*.zip')):   # tools/build_test_saves.sh, when built
        assets += [pack.name, pack.name + '.sha256']
    notes_file = 'docs/RELEASE-NOTES-%s.md' % version
    command = ('gh release create %s --repo TommoDotCommo/death-stranding-coop --prerelease --verify-tag \\\n'
               '    --title "Death Stranding Co-op %s (alpha)" --notes-file %s \\\n    %s\n'
               % (version, version, notes_file, ' \\\n    '.join('local/release/' + a for a in assets)))
    (out / ('PUBLISH-%s.txt' % version)).write_text(
        'Death Stranding Co-op %s: how to publish (the release script never publishes)\n\n'
        'Publishing needs the owner\'s go. Then, from the repository root, after pushing the tag %s:\n\n'
        '  git tag -a %s -m "Death Stranding Co-op %s" && git push origin %s\n\n%s\n'
        'Before: run python3 tools/check_release_package.py local/release/%s.zip once more, read the release notes\n'
        '(%s: verified and unverified kept apart) and try the single-file Setup on a clean Windows user.\n'
        'Nexus Mods (optional, same zip, same version string): NEXUS-%s.txt.\n'
        % (version, version, version, version, version, command, name, notes_file, version))


def main(argv):
    if len(argv) == 8 and argv[1] == 'stage':
        stage(*argv[2:])
    elif len(argv) == 3 and argv[1] == 'payload-args':
        print('\n'.join(payload_args(argv[2])))
    elif len(argv) == 4 and argv[1] == 'zip':
        make_zip(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == 'sha256':
        write_sha256(argv[2])
    elif len(argv) == 4 and argv[1] == 'notes':
        notes(argv[2], argv[3])
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
