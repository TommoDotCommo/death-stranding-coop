#!/usr/bin/env python3
"""coop-mod.json: what the package is and where each file goes, for Setup, the launcher, diagnostics and mod managers
(co-op mod standard J5). The launcher's install core (launcher/install.cpp) reads it; tools/release.sh writes it.

  mod_manifest.py <version> <staged package folder>   prints the manifest of the staged release
  mod_manifest.py --check <zip>                       checks the manifest in a release zip against the zip's files
  mod_manifest.py --nexus-note <zip>                  prints the copy note for Nexus Mods (same zip, same version)

"files" lists every installed file: root "game" is the game folder (only the proxy goes there), root "data" the co-op data
folder %LOCALAPPDATA%\\DeathStrandingCoop. A data path starting "app/" is the launcher's folder; its file is the package's
file of the same name without "app/" (launcher/install.h). The supported game build is the launcher's and the mod's pinned
ds.exe hash, read from mod/discovery_options.h: one source of truth.
"""
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ID = 'death-stranding-coop'
NAME = 'Death Stranding Co-op'
# (root, installed path); the package file is the path without "app/"
LAYOUT = [('game', 'avrt.dll'), ('data', 'redirect-data/mod/mod.dll'),
          ('data', 'app/deathstranding-coop.exe'), ('data', 'app/coop-world.exe'), ('data', 'app/avrt.dll'),
          ('data', 'app/redirect-data/mod/mod.dll'), ('data', 'app/README.txt'), ('data', 'app/LICENSE.txt'),
          ('data', 'app/THIRD-PARTY-NOTICES.txt')]
KEYS = ['schema', 'id', 'name', 'version', 'game', 'proxy', 'proton', 'files', 'user_data', 'homepage', 'updates']


def package_path(path):
    return path[4:] if path.startswith('app/') else path


def pinned_hash():
    """The ds.exe SHA-256 the launcher and the mod accept (mod/discovery_options.h PinnedHash)."""
    text = (ROOT / 'mod' / 'discovery_options.h').read_text()
    found = re.findall(r'PinnedHash\s*=\s*"([0-9a-f]{64})"', text)
    if len(found) != 1:
        raise SystemExit('mod/discovery_options.h: expected exactly one PinnedHash')
    return found[0]


def manifest(version, files):
    """files: {package path: bytes}."""
    return {
        'schema': 1,
        'id': ID,
        'name': NAME,
        'version': version,
        'game': {'name': "DEATH STRANDING DIRECTOR'S CUT", 'steam_appid': 1850570, 'exe': ['ds.exe'],
                 'builds': [{'name': 'Steam build 13300582', 'sha256': pinned_hash()}]},
        # The proxy does not chain-load another avrt.dll (redirect/ has no chain support): chain_file is null and Setup
        # refuses a foreign avrt.dll instead of keeping it.
        'proxy': {'file': 'avrt.dll', 'system_dll': 'avrt.dll', 'chain_file': None},
        'proton': {'dll_overrides': {'avrt': 'native,builtin'}},
        'files': [{'path': p, 'sha256': hashlib.sha256(files[package_path(p)]).hexdigest(),
                   'size': len(files[package_path(p)]), 'root': root} for root, p in LAYOUT],
        'user_data': '%LOCALAPPDATA%\\DeathStrandingCoop',
        'homepage': 'https://github.com/TommoDotCommo/death-stranding-coop',
        'updates': {'github': 'TommoDotCommo/death-stranding-coop', 'nexus': None},
    }


def check(zip_path):
    """The manifest in the zip's top folder: schema-complete, the pinned build, and every listed file's hash and size
    equal the zip's member. Returns the manifest; raises AssertionError."""
    zip_path = Path(zip_path)
    top = zip_path.name[:-len('.zip')] + '/'
    with zipfile.ZipFile(zip_path) as z:
        m = json.loads(z.read(top + 'coop-mod.json'))
        assert list(m) == KEYS, 'coop-mod.json keys differ from the standard: %s' % list(m)
        assert m['schema'] == 1 and m['id'] == ID, 'coop-mod.json schema or id'
        assert m['version'] == zip_path.name[len('deathstranding-coop-'):-len('.zip')], 'coop-mod.json version differs from the zip name'
        assert sorted(m['game']) == ['builds', 'exe', 'name', 'steam_appid'], 'coop-mod.json game keys'
        assert [b['sha256'] for b in m['game']['builds']] == [pinned_hash()], 'coop-mod.json build differs from the pinned ds.exe hash'
        assert sorted(m['proxy']) == ['chain_file', 'file', 'system_dll'], 'coop-mod.json proxy keys'
        assert sorted(m['updates']) == ['github', 'nexus'], 'coop-mod.json updates keys'
        assert [(f['root'], f['path']) for f in m['files']] == LAYOUT, 'coop-mod.json file list'
        for f in m['files']:
            assert sorted(f) == ['path', 'root', 'sha256', 'size'], 'coop-mod.json file entry keys'
            data = z.read(top + package_path(f['path']))
            assert hashlib.sha256(data).hexdigest() == f['sha256'], 'coop-mod.json hash does not match the zip: ' + f['path']
            assert len(data) == f['size'], 'coop-mod.json size does not match the zip: ' + f['path']
    return m


def nexus_note(zip_path):
    zip_path = Path(zip_path)
    m = check(zip_path)
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    return ('Death Stranding Co-op %(v)s for Nexus Mods (copy note; nothing is uploaded by the release script)\n\n'
            'File to upload: %(zip)s (the same file as on GitHub Releases, unchanged)\n'
            'SHA-256: %(sha)s\n'
            'Version field: %(v)s (exactly this string; Setup compares it)\n'
            "Requirements: DEATH STRANDING DIRECTOR'S CUT on Steam, build 13300582. Alpha: for testers.\n\n"
            'Install notes: the zip has one top folder, %(top)s/. Players run DeathStrandingCoop-Setup.exe from it (or the\n'
            'single-file Setup from GitHub). Not a drop-in for mod managers: the mod also needs the co-op launcher and its\n'
            'data folder, which Setup puts in %%LOCALAPPDATA%%\\DeathStrandingCoop. coop-mod.json lists every file.\n\n'
            'Canonical release: https://github.com/TommoDotCommo/death-stranding-coop/releases/tag/%(v)s\n'
            % {'v': m['version'], 'zip': zip_path.name, 'sha': digest, 'top': zip_path.name[:-len('.zip')]})


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--check':
        try:
            check(sys.argv[2])
        except (AssertionError, KeyError) as e:
            print('FAIL: %s' % e, file=sys.stderr)
            sys.exit(1)
        print('PASS: coop-mod.json matches the zip')
    elif len(sys.argv) == 3 and sys.argv[1] == '--nexus-note':
        sys.stdout.write(nexus_note(sys.argv[2]))
    elif len(sys.argv) == 3:
        stage = Path(sys.argv[2])
        print(json.dumps(manifest(sys.argv[1], {package_path(p): (stage / package_path(p)).read_bytes() for _, p in LAYOUT}), indent=1))
    else:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
