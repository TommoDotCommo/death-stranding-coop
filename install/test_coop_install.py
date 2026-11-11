"""Install/uninstall/verify against temporary FAKE game folders only. Never the real game, never real saves."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

import coop_install as ci
import make_package

EXE = b'MZ fake game executable\n' * 100
EXE_HASH = hashlib.sha256(EXE).hexdigest()
IDLE = lambda game, exe: False
BUSY = lambda game, exe: True


def snapshot(root):
    """Every path below root with its bytes (files) or marker (folders): byte-for-byte comparison."""
    out = {}
    for p in sorted(Path(root).rglob('*')):
        out[str(p.relative_to(root))] = p.read_bytes() if p.is_file() else None
    return out


class Base(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        self.game = self.root/'game'
        self.data = self.root/'appdata'/'DeathStrandingCoop'
        self.game.mkdir()
        (self.game/'ds.exe').write_bytes(EXE)
        (self.game/'d3dcompiler_47.dll').write_bytes(b'the games own compiler')
        (self.game/'data').mkdir()
        (self.game/'data'/'pack.bin').write_bytes(b'\x00\x01' * 50)
        self.make_package('1.0', b'proxy-v1', b'mod-v1')

    def make_package(self, version, proxy, mod):
        src = self.root/f'src-{version}'
        src.mkdir(exist_ok=True)
        (src/'proxy.dll').write_bytes(proxy)
        (src/'mod.dll').write_bytes(mod)
        self.package = self.root/f'package-{version}'
        make_package.build_package(self.package, version, src/'proxy.dll', src/'mod.dll',
                                   builds=[dict(name='test build', sha256=EXE_HASH)])

    def install(self):
        return ci.install(self.package, self.game, self.data, running=IDLE)

    def uninstall(self):
        return ci.uninstall(self.game, running=IDLE)


class InstallUninstall(Base):
    def test_optional_launcher_is_installed_verified_and_uninstalled(self):
        launcher = self.root/'launcher.exe'
        launcher.write_bytes(b'MZ fake launcher')
        world = self.root/'world.exe'
        world.write_bytes(b'MZ fake world server')
        self.package = self.root/'package-with-launcher'
        make_package.build_package(self.package, '1.0', self.root/'src-1.0/proxy.dll',
                                   self.root/'src-1.0/mod.dll', launcher=launcher, world=world,
                                   builds=[dict(name='test build', sha256=EXE_HASH)])
        before = snapshot(self.game)
        self.install()
        self.assertEqual((self.game/'deathstranding-coop.exe').read_bytes(), launcher.read_bytes())
        self.assertEqual((self.game/'coop-world.exe').read_bytes(), world.read_bytes())   # beside the launcher, which starts it
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.UP_TO_DATE)
        self.uninstall()
        self.assertEqual(snapshot(self.game), before)

    def test_install_adds_files_and_never_touches_shipped_ones(self):
        before = snapshot(self.game)
        report = self.install()
        self.assertEqual(report['state'], ci.UP_TO_DATE)
        after = snapshot(self.game)
        for path, content in before.items():
            self.assertEqual(after[path], content, f'shipped file changed: {path}')
        added = set(after) - set(before)
        self.assertEqual(added, {'avrt.dll', 'coop-mod.json', ci.RECEIPT})
        self.assertEqual((self.game/'avrt.dll').read_bytes(), b'proxy-v1')
        self.assertEqual((self.data/'redirect-data'/'mod'/'mod.dll').read_bytes(), b'mod-v1')

    def test_uninstall_restores_the_game_folder_byte_for_byte(self):
        before = snapshot(self.game)
        self.install()
        removed, kept = self.uninstall()
        self.assertEqual(kept, [])
        self.assertEqual(snapshot(self.game), before)
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.NOT_INSTALLED)

    def test_uninstall_removes_every_listed_file_and_empty_folders_but_keeps_saves(self):
        self.install()
        save = self.data/'redirect-data'/'local'/'save.dat'
        save.parent.mkdir(parents=True)
        save.write_bytes(b'co-op save')
        self.uninstall()
        self.assertFalse((self.data/'redirect-data'/'mod'/'mod.dll').exists())
        self.assertFalse((self.data/'redirect-data'/'mod').exists())
        self.assertEqual(save.read_bytes(), b'co-op save')

    def test_uninstall_with_nothing_but_our_data_removes_the_data_folders_it_made(self):
        self.install()
        self.uninstall()
        self.assertFalse((self.data).exists() and any(self.data.rglob('*')))

    def test_existing_proxy_name_is_a_conflict_and_nothing_changes(self):
        (self.game/'avrt.dll').write_bytes(b'another mods avrt')
        before = snapshot(self.game)
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.CONFLICT)
        with self.assertRaises(ci.InstallError) as e:
            self.install()
        self.assertEqual(e.exception.code, ci.FAILED)
        self.assertEqual(snapshot(self.game), before)
        self.assertFalse(self.data.exists())

    def test_case_variant_of_the_proxy_name_is_a_conflict(self):
        (self.game/'AVRT.DLL').write_bytes(b'x')
        before = snapshot(self.game)
        with self.assertRaises(ci.InstallError):
            self.install()
        self.assertEqual(snapshot(self.game), before)

    def test_unsupported_build_and_missing_exe(self):
        (self.game/'ds.exe').write_bytes(b'another build')
        with self.assertRaises(ci.InstallError) as e:
            self.install()
        self.assertEqual(e.exception.code, ci.UNSUPPORTED_BUILD)
        (self.game/'ds.exe').unlink()
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.NOT_A_GAME_FOLDER)

    def test_running_game_blocks_install_and_uninstall(self):
        before = snapshot(self.game)
        with self.assertRaises(ci.InstallError) as e:
            ci.install(self.package, self.game, self.data, running=BUSY)
        self.assertEqual(e.exception.code, ci.GAME_RUNNING)
        self.assertEqual(snapshot(self.game), before)
        self.install()
        installed = snapshot(self.game)
        with self.assertRaises(ci.InstallError) as e:
            ci.uninstall(self.game, running=BUSY)
        self.assertEqual(e.exception.code, ci.GAME_RUNNING)
        self.assertEqual(snapshot(self.game), installed)

    def test_failed_install_rolls_back_completely(self):
        before = snapshot(self.game)
        (self.package/'redirect-data'/'mod'/'mod.dll').write_bytes(b'tampered package')
        with self.assertRaises(ci.InstallError):
            self.install()
        self.assertEqual(snapshot(self.game), before)
        # failure in the middle of copying (second file cannot be written): the first is removed again
        self.make_package('1.1', b'proxy-v1', b'mod-v1')
        blocker = self.root/'blocked'
        blocker.write_bytes(b'a file where the data folder should be')
        with self.assertRaises(Exception):
            ci.install(self.package, self.game, blocker, running=IDLE)
        self.assertEqual(snapshot(self.game), before)

    def test_unsafe_manifest_paths_are_refused(self):
        manifest = json.loads((self.package/ci.MANIFEST).read_text())
        for bad in ('../escape.dll', '/abs.dll', 'C:/x.dll', 'a//b.dll'):
            manifest['files'][0]['path'] = bad
            (self.package/ci.MANIFEST).write_text(json.dumps(manifest))
            with self.assertRaises(ci.InstallError):
                ci.status(self.package, self.game, self.data)


class StateAndRepair(Base):
    def test_states(self):
        st = lambda: ci.status(self.package, self.game, self.data)['state']
        self.assertEqual(st(), ci.NOT_INSTALLED)
        self.install()
        self.assertEqual(st(), ci.UP_TO_DATE)
        (self.game/'avrt.dll').write_bytes(b'changed')
        report = ci.status(self.package, self.game, self.data)
        self.assertEqual(report['state'], ci.NEEDS_REPAIR)
        self.assertIn('modified: avrt.dll', report['problems'])
        (self.game/'avrt.dll').unlink()
        self.assertIn('missing: avrt.dll', ci.status(self.package, self.game, self.data)['problems'])

    def test_repair_restores_our_files_only(self):
        self.install()
        (self.game/'avrt.dll').write_bytes(b'changed')
        shipped = (self.game/'ds.exe').read_bytes()
        self.install()
        self.assertEqual((self.game/'avrt.dll').read_bytes(), b'proxy-v1')
        self.assertEqual((self.game/'ds.exe').read_bytes(), shipped)
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.UP_TO_DATE)

    def test_update_replaces_our_files_and_uninstall_still_restores(self):
        before = snapshot(self.game)
        self.install()
        self.make_package('2.0', b'proxy-v2', b'mod-v2')
        self.assertEqual(ci.status(self.package, self.game, self.data)['state'], ci.UPDATE_AVAILABLE)
        self.assertEqual(self.install()['state'], ci.UP_TO_DATE)
        self.assertEqual((self.game/'avrt.dll').read_bytes(), b'proxy-v2')
        self.assertFalse(list(self.game.glob('*.coop-*')))
        self.uninstall()
        self.assertEqual(snapshot(self.game), before)

    def test_install_twice_is_a_no_op(self):
        self.install()
        first = snapshot(self.game)
        self.install()
        self.assertEqual(snapshot(self.game), first)

    def test_modified_file_is_kept_by_uninstall_and_reported(self):
        self.install()
        (self.game/'avrt.dll').write_bytes(b'player edited')
        removed, kept = self.uninstall()
        self.assertEqual(kept, ['avrt.dll'])
        self.assertEqual((self.game/'avrt.dll').read_bytes(), b'player edited')
        self.assertTrue((self.game/ci.RECEIPT).exists())   # so a later uninstall can finish the job

    def test_mod_manager_install_is_recognised_and_left_alone(self):
        for entry in json.loads((self.package/ci.MANIFEST).read_text())['files']:
            if entry['root'] == 'game':
                (self.game/entry['path']).write_bytes((self.package/entry['path']).read_bytes())
        before = snapshot(self.game)
        report = ci.status(self.package, self.game, self.data)
        self.assertEqual(report['state'], ci.BY_MOD_MANAGER)
        self.install()
        self.assertEqual(snapshot(self.game), before)
        with self.assertRaises(ci.InstallError):
            self.uninstall()
        os.link(self.game/'avrt.dll', self.root/'vortex-staging.dll')
        self.assertIn('Vortex', ci.status(self.package, self.game, self.data)['installed_by'])


class Cli(Base):
    def run_cli(self, *args):
        return ci.main([*args, '--package', str(self.package)])

    def test_exit_codes(self):
        g = str(self.game)
        self.assertEqual(self.run_cli('install', g, '--data-dir', str(self.data)), ci.USAGE)   # no --yes
        self.assertEqual(self.run_cli('status', g, '--data-dir', str(self.data), '--json'), ci.OK)
        self.assertEqual(self.run_cli('install', g, '--data-dir', str(self.data), '--yes'), ci.OK)
        self.assertEqual(self.run_cli('verify', g), ci.OK)
        (self.game/'avrt.dll').write_bytes(b'x')
        self.assertEqual(self.run_cli('verify', g), ci.FAILED)
        self.assertEqual(self.run_cli('nonsense', g), ci.USAGE)
        (self.game/'ds.exe').write_bytes(b'other build')
        self.assertEqual(self.run_cli('status', g), ci.UNSUPPORTED_BUILD)


if __name__ == '__main__':
    unittest.main()
