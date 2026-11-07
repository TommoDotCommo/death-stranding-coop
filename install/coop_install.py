#!/usr/bin/env python3
"""Install, uninstall and verify the co-op package in a game folder. Game-free, standard library only.

The package mirrors the game folder (rule J1 of the co-op mod standard): a proxy named after a system DLL the game
imports but does not ship, plus `coop-mod.json` (J5). Installing only ADDS files. It never edits, renames or replaces
a file that is already in the game folder: if a destination exists and is not ours, install stops with a conflict.
Uninstalling removes exactly the files the receipt lists (and the folders install created, when empty), so the game
folder is byte-for-byte what it was. Files with `"root": "data"` go to the co-op data folder (outside the game folder,
`%LOCALAPPDATA%\\DeathStrandingCoop`); uninstall removes only those files and keeps everything else there (saves).

Verbs (J9):  status [--json] | verify | install --yes | uninstall --yes      Exit codes:
0 ok, 1 failed, 2 usage, 3 unsupported game build, 4 game running.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

MANIFEST = 'coop-mod.json'
RECEIPT = 'coop-mod.receipt.json'
OK, FAILED, USAGE, UNSUPPORTED_BUILD, GAME_RUNNING = 0, 1, 2, 3, 4

# States of status(): what the one Setup button says (G2).
NOT_A_GAME_FOLDER = 'not-a-game-folder'
UNSUPPORTED = 'unsupported-build'
NOT_INSTALLED = 'not-installed'
UP_TO_DATE = 'up-to-date'
UPDATE_AVAILABLE = 'update-available'
NEEDS_REPAIR = 'needs-repair'
CONFLICT = 'conflict'
BY_MOD_MANAGER = 'installed-by-mod-manager'


class InstallError(Exception):
    def __init__(self, message, code=FAILED):
        super().__init__(message)
        self.code = code


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def load_manifest(package):
    path = Path(package)/MANIFEST
    try:
        manifest = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        raise InstallError(f'package manifest unreadable: {e}', USAGE)
    if manifest.get('schema') != 1 or not manifest.get('id') or not manifest.get('files'):
        raise InstallError('package manifest has no schema 1 / id / files', USAGE)
    for entry in manifest['files']:
        safe_relative(entry['path'])
        if entry.get('root', 'game') not in ('game', 'data'):
            raise InstallError(f"unknown root in {entry['path']}", USAGE)
    return manifest


def safe_relative(rel):
    """A package path must stay below its root: relative, no '..', no drive, no empty parts."""
    parts = rel.replace('\\', '/').split('/')
    if (not rel or rel.startswith(('/', '\\')) or ':' in rel or any(p in ('', '.', '..') for p in parts)):
        raise InstallError(f'unsafe path in manifest: {rel!r}', USAGE)
    return Path(*parts)


def default_game_running(game_dir, exe):
    """True when the game executable is running. Windows refuses write access to a running image; on Linux the
    executable is looked up in /proc. Opening for update changes nothing (no write is made)."""
    path = Path(game_dir)/exe
    if os.name == 'nt':
        try:
            with open(path, 'r+b'):
                return False
        except PermissionError:
            return True
        except OSError:
            return False
    proc = Path('/proc')
    if proc.is_dir():
        for entry in proc.iterdir():
            if entry.name.isdigit():
                try:
                    if os.path.samefile(entry/'exe', path):
                        return True
                except OSError:
                    pass
    return False


class Target:
    """The folders an install writes to: the game folder and the co-op data folder."""

    def __init__(self, game_dir, data_dir):
        self.roots = {'game': Path(game_dir), 'data': Path(data_dir) if data_dir else None}

    def path(self, root, rel):
        base = self.roots[root]
        if base is None:
            raise InstallError('this package installs data files: give the co-op data folder (--data-dir)', USAGE)
        return base/safe_relative(rel)


def exists_any_case(path):
    """Windows file names are case-insensitive: 'AVRT.dll' is 'avrt.dll'."""
    if os.path.lexists(path):
        return True
    try:
        return path.name.lower() in {n.lower() for n in os.listdir(path.parent)}
    except OSError:
        return False


def read_receipt(game_dir):
    path = Path(game_dir)/RECEIPT
    if not path.exists():
        return None
    try:
        receipt = json.loads(path.read_text(encoding='utf-8'))
        receipt['files']
        return receipt
    except (OSError, ValueError, KeyError):
        raise InstallError('receipt unreadable; reinstall with a repair', FAILED)


def game_files(manifest):
    return [e for e in manifest['files'] if e.get('root', 'game') == 'game']


def status(package, game_dir, data_dir=None):
    """Describe the state of `game_dir` against `package`. Reads only; hashes files, changes nothing."""
    manifest = load_manifest(package)
    game_dir = Path(game_dir)
    report = dict(state=None, id=manifest['id'], package_version=manifest.get('version'), installed_version=None,
                  problems=[], conflicts=[], installed_by='', unsupported_build=None)
    exe = manifest['game']['exe'][0]
    if not (game_dir/exe).is_file():
        report['state'] = NOT_A_GAME_FOLDER
        report['problems'].append(f'{exe} not found in the game folder')
        return report
    builds = {b['sha256'].lower(): b['name'] for b in manifest['game'].get('builds', [])}
    digest = sha256_file(game_dir/exe)
    report['game_sha256'] = digest
    report['game_build'] = builds.get(digest)
    if builds and digest not in builds:
        report['state'] = UNSUPPORTED
        report['problems'].append('this game build is not supported; co-op stays off')
        return report
    receipt = read_receipt(game_dir)
    target = Target(game_dir, data_dir or (receipt or {}).get('data_dir'))
    if receipt:
        report['installed_by'] = 'setup'
        report['installed_version'] = receipt.get('version')
        for entry in receipt['files']:
            p = target.path(entry['root'], entry['path'])
            if not p.is_file():
                report['problems'].append(f"missing: {entry['path']}")
            elif sha256_file(p) != entry['sha256']:
                report['problems'].append(f"modified: {entry['path']}")
        if report['problems']:
            report['state'] = NEEDS_REPAIR
        else:
            wanted = {(e.get('root', 'game'), e['path']): e['sha256'] for e in manifest['files']}
            have = {(e['root'], e['path']): e['sha256'] for e in receipt['files'] if e['path'] != MANIFEST}
            report['state'] = UP_TO_DATE if wanted == have and receipt.get('version') == manifest.get('version') \
                else UPDATE_AVAILABLE
        return report
    # No receipt: nothing of ours, a conflicting file, or a drop-in install by a mod manager or by hand (J6).
    matching, missing = [], []
    for entry in game_files(manifest):
        p = target.path('game', entry['path'])
        if not exists_any_case(p):
            missing.append(entry['path'])
        elif p.is_file() and sha256_file(p) == entry['sha256']:
            matching.append(p)
        else:
            report['conflicts'].append(entry['path'])
    if report['conflicts']:
        report['state'] = CONFLICT
        report['problems'].append('a file with the same name is already in the game folder and is not ours; '
                                  'nothing was changed: ' + ', '.join(report['conflicts']))
    elif matching and not missing:
        report['state'] = BY_MOD_MANAGER
        report['installed_by'] = 'mod manager or by hand'
        if any(p.stat().st_nlink > 1 for p in matching):
            report['installed_by'] = 'mod manager (hard-linked files, likely Vortex)'
    elif matching:
        report['state'] = NEEDS_REPAIR
        report['problems'] += [f'missing: {m}' for m in missing]
    else:
        report['state'] = NOT_INSTALLED
    return report


def _write_atomic(source, dest):
    tmp = dest.with_name(dest.name+'.coop-tmp')
    shutil.copyfile(source, tmp)
    os.replace(tmp, dest)


def check_package(package, manifest):
    for entry in manifest['files']:
        p = Path(package)/safe_relative(entry['path'])
        if not p.is_file() or sha256_file(p) != entry['sha256'] or p.stat().st_size != entry['size']:
            raise InstallError(f"package file {entry['path']} is missing or does not match the manifest")


def install(package, game_dir, data_dir=None, running=default_game_running):
    """Add the package to the game folder. Returns the status report afterwards. All-or-nothing."""
    manifest = load_manifest(package)
    before = status(package, game_dir, data_dir)
    exe = manifest['game']['exe'][0]
    if before['state'] == NOT_A_GAME_FOLDER:
        raise InstallError(before['problems'][0])
    if before['state'] == UNSUPPORTED:
        raise InstallError(before['problems'][0], UNSUPPORTED_BUILD)
    if before['state'] == CONFLICT:
        raise InstallError(before['problems'][0])
    if before['state'] in (UP_TO_DATE, BY_MOD_MANAGER):
        return before
    if running(game_dir, exe):
        raise InstallError('the game is running; close it first', GAME_RUNNING)
    check_package(package, manifest)
    receipt = read_receipt(game_dir)
    target = Target(game_dir, data_dir or (receipt or {}).get('data_dir'))
    plan = [(e.get('root', 'game'), e['path'], Path(package)/safe_relative(e['path']), e['sha256'])
            for e in manifest['files']]
    plan.append(('game', MANIFEST, Path(package)/MANIFEST, sha256_file(Path(package)/MANIFEST)))
    owned = {(e['root'], e['path']) for e in (receipt or {}).get('files', [])}
    for root, rel, _, _ in plan:   # a destination that exists and is not already ours is never touched
        p = target.path(root, rel)
        if exists_any_case(p) and (root, rel) not in owned:
            raise InstallError(f'{rel} already exists and is not ours; nothing was changed')
    created_files, created_dirs, replaced = [], [], []
    try:
        for root, rel, source, digest in plan:
            dest = target.path(root, rel)
            missing = []
            parent = dest.parent
            while not parent.exists():
                missing.append(parent)
                parent = parent.parent
            for d in reversed(missing):
                d.mkdir()
                created_dirs.append((root, d))
            if (root, rel) in owned:   # an older copy of our own file: keep it until the new one is in place
                backup = dest.with_name(dest.name+'.coop-old')
                shutil.copyfile(dest, backup)
                replaced.append((dest, backup))
            else:
                created_files.append(dest)
            _write_atomic(source, dest)
        files = [dict(root=root, path=rel, sha256=digest, size=target.path(root, rel).stat().st_size)
                 for root, rel, _, digest in plan]
        new_paths = {(f['root'], f['path']) for f in files}
        stale = [(root, rel) for (root, rel) in owned if (root, rel) not in new_paths]
        old_dirs = [d['path'] for d in (receipt or {}).get('dirs', [])]
        dirs = [dict(path=str(d)) for _, d in created_dirs] + [dict(path=p) for p in old_dirs]
        record = dict(schema=1, id=manifest['id'], version=manifest.get('version'), installer='setup', exe=exe,
                      data_dir=str(target.roots['data']) if target.roots['data'] else None, files=files, dirs=dirs)
        receipt_path = Path(game_dir)/RECEIPT
        if not receipt:
            created_files.append(receipt_path)
        tmp = receipt_path.with_name(RECEIPT+'.coop-tmp')
        tmp.write_text(json.dumps(record, indent=1, sort_keys=True), encoding='utf-8')
        os.replace(tmp, receipt_path)
        for root, rel in stale:
            p = target.path(root, rel)
            if p.is_file():
                p.unlink()
        for _, backup in replaced:
            backup.unlink()
    except BaseException:
        for dest, backup in replaced:       # put our own previous files back
            if backup.exists():
                os.replace(backup, dest)
        for p in created_files:
            for q in (p, p.with_name(p.name+'.coop-tmp')):
                if q.exists():
                    q.unlink()
        for _, d in reversed(created_dirs):
            try:
                d.rmdir()
            except OSError:
                pass
        raise
    return status(package, game_dir, data_dir)


def uninstall(game_dir, running=default_game_running):
    """Remove what the receipt lists. Returns (removed, kept). Files changed since install are kept and reported."""
    game_dir = Path(game_dir)
    receipt = read_receipt(game_dir)
    if receipt is None:
        raise InstallError('not installed by Setup; if a mod manager installed it, remove it there')
    if running(game_dir, receipt.get('exe', 'ds.exe')):
        raise InstallError('the game is running; close it first', GAME_RUNNING)
    target = Target(game_dir, receipt.get('data_dir'))
    removed, kept = [], []
    for entry in receipt['files']:
        p = target.path(entry['root'], entry['path'])
        if not p.is_file():
            removed.append(entry['path'])      # already gone
        elif sha256_file(p) != entry['sha256']:
            kept.append(entry)                 # the player changed it: not ours to delete
        else:
            p.unlink()
            removed.append(entry['path'])
    leftovers = {d['path'] for d in kept}
    for d in sorted(receipt.get('dirs', []), key=lambda d: -len(Path(d['path']).parts)):
        try:
            Path(d['path']).rmdir()                          # only when empty; saves in the data folder keep it alive
        except OSError:
            pass
    receipt_path = game_dir/RECEIPT
    if kept:
        receipt['files'] = kept
        receipt_path.write_text(json.dumps(receipt, indent=1, sort_keys=True), encoding='utf-8')
    else:
        receipt_path.unlink()
    return removed, sorted(leftovers)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('verb', choices=('status', 'verify', 'install', 'uninstall'))
    p.add_argument('game_dir')
    p.add_argument('--package', default=str(Path(__file__).resolve().parent/'package'),
                   help='folder holding coop-mod.json and the files it lists')
    p.add_argument('--data-dir', help='co-op data folder (defaults to the one in the receipt)')
    p.add_argument('--json', action='store_true')
    p.add_argument('--yes', action='store_true', help='required by install and uninstall')
    try:
        a = p.parse_args(argv)
    except SystemExit as e:
        return USAGE if e.code else OK
    try:
        if a.verb in ('install', 'uninstall') and not a.yes:
            raise InstallError(f'{a.verb} needs --yes', USAGE)
        if a.verb == 'uninstall':
            removed, kept = uninstall(a.game_dir)
            print(json.dumps(dict(removed=removed, kept_modified=kept)) if a.json else
                  f'removed {len(removed)} files' + (f'; kept (changed since install): {", ".join(kept)}' if kept else ''))
            return FAILED if kept else OK
        report = install(a.package, a.game_dir, a.data_dir) if a.verb == 'install' else \
            status(a.package, a.game_dir, a.data_dir)
        print(json.dumps(report, indent=1) if a.json else report['state'] + ''.join(f'\n  {m}' for m in report['problems']))
        if a.verb == 'install':
            return OK
        if report['state'] == UNSUPPORTED:
            return UNSUPPORTED_BUILD
        return OK if report['state'] in (UP_TO_DATE, NOT_INSTALLED, UPDATE_AVAILABLE, BY_MOD_MANAGER) else FAILED
    except InstallError as e:
        print(f'error: {e}', file=sys.stderr)
        return e.code


if __name__ == '__main__':
    sys.exit(main())
