#!/usr/bin/env python3
"""Game-free tests for make_test_saves.py: its save container against the launcher's synthetic vectors, the pack it
writes, every refusal, and mutants of the builder that the same oracles must catch (run side by side)."""
import contextlib
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'mod'))
import make_test_saves as builder  # noqa: E402
from mutants import parallel  # noqa: E402


def vectors():
    """The synthetic saves of launcher/saves_vectors.h (no game data)."""
    text = (ROOT/'launcher'/'saves_vectors.h').read_text()
    return {name: bytes(int(h, 16) for h in re.findall(r'\\x([0-9a-f]{2})', body))
            for name, body in re.findall(r'static const char (\w+)\[\]="([^"]*)"', text)}


V = vectors()
USER_A, USER_B = 'steam_76561197960265729', 'steam_76561198000000000'


def big(extra, seed):
    """A valid checkpoint with `extra` bytes more in its block (like saves_test.cpp)."""
    a = V['VectorA']
    meta, picture = int(a[0:8].split(b'\0')[0]), int(a[8:16].split(b'\0')[0])
    block = a[24 + meta + picture:] + bytes((seed * 7 + i * 13) & 255 for i in range(extra))
    head = b''.join(str(v).encode().ljust(8, b'\0') for v in (meta, picture, len(block)))
    return head + a[24:24 + meta + picture] + block


def source_tree(folder, extra=None):
    """A source folder with two sets; `extra(folder)` adds whatever a case needs."""
    folder = Path(folder)
    (folder/'ep1'/'manualsave0').mkdir(parents=True)
    (folder/'ep1'/'AutoSave2').mkdir(parents=True)
    (folder/'late'/'quicksave1').mkdir(parents=True)
    (folder/'ep1'/'manualsave0'/'checkpoint.dat').write_bytes(V['VectorA'])
    (folder/'ep1'/'AutoSave2'/'checkpoint.dat').write_bytes(big(3000, 4))
    (folder/'late'/'quicksave1'/'checkpoint.dat').write_bytes(V['VectorB'])
    sets = [dict(id='ep1', title='Episode one', description='Synthetic.\nTwo lines.', episode=1, play_time='2 h',
                 credit='test', **{'continue': 'manualsave0'}),
            dict(id='late', title='Late game', episode=14, play_time=80)]
    (folder/'sets.json').write_text(json.dumps(dict(sets=sets)))
    if extra:
        extra(folder)
    return folder


def refused(module, extra):
    with tempfile.TemporaryDirectory() as d:
        src = source_tree(Path(d)/'src', extra)
        out = Path(d)/'out.zip'
        try:
            module.build(src, out, '0.1.0-alpha.1')
        except module.Refused:
            return not out.exists() and not [p for p in Path(d).iterdir() if p.name.startswith('.test-saves-')]
        return False


def add(path, data=None, folder=False):
    def change(src):
        p = src/path
        if folder:
            p.mkdir(parents=True)
        else:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data if data is not None else V['VectorA'])
    return change


# What the builder must refuse: each a source folder that is not only checkpoint folders.
REFUSALS = {
    'profile file': add('ep1/manualsave0/profile.dat', V['VectorProfile']),
    'other file': add('ep1/manualsave0/notes.txt', b'hello'),
    'profile folder': add('ep1/profile/profile.dat', V['VectorProfile']),
    'other folder': add('ep1/notes/checkpoint.dat'),
    'loose file in a set': add('ep1/readme.txt', b'x'),
    'stray in source': add('stray.txt', b'x'),
    'unlisted set': add('ep9/manualsave0/checkpoint.dat'),
    'profile as checkpoint': add('ep1/quicksave3/checkpoint.dat', V['VectorProfile']),
    'junk as checkpoint': add('ep1/quicksave3/checkpoint.dat', b'0' * 100),
    'empty checkpoint folder': add('ep1/quicksave4', folder=True),
}


def link(src):
    os.symlink(src/'ep1'/'manualsave0', src/'ep1'/'manualsave7')


REFUSALS['linked folder'] = link

# Mutants: (source text, replacement, the refusal or check that must catch it).
MUTANTS = {
    'folder-name': ('or not SAVE_FOLDER.match(entry):', ':', 'other folder'),
    'only-checkpoint': ("if inside != ['checkpoint.dat'] or", "if 'checkpoint.dat' not in inside or", 'profile file'),
    'content': ('                data = resign(data, PLACEHOLDER)\n', '                pass\n', 'profile as checkpoint'),
    'stray': ("        if entry not in ids:\n", "        if False:\n", 'stray in source'),
    'placeholder': ('data = resign(data, PLACEHOLDER)', 'resign(data, PLACEHOLDER)', 'account'),
    'hash': ('sha256=hashlib.sha256(f[\'data\']).hexdigest()', 'sha256=hashlib.sha256(f[\'folder\'].encode()).hexdigest()', 'manifest'),
}


def placeholder_oracle(module):
    with tempfile.TemporaryDirectory() as d:
        out = Path(d)/'out.zip'
        module.build(source_tree(Path(d)/'src'), out, 'v')
        with zipfile.ZipFile(out) as z:
            return all(builder.account_of(z.read(n)) == builder.PLACEHOLDER for n in z.namelist() if n.endswith('.dat'))


def manifest_oracle(module):
    with tempfile.TemporaryDirectory() as d:
        out = Path(d)/'out.zip'
        module.build(source_tree(Path(d)/'src'), out, 'v')
        with zipfile.ZipFile(out) as z:
            m = json.loads(z.read('test-saves.json'))
            return all(hashlib.sha256(z.read(f['path'])).hexdigest() == f['sha256'] and len(z.read(f['path'])) == f['size']
                       for s in m['sets'] for f in s['files'])


def mutant(kind):
    before, after, oracle = MUTANTS[kind]
    path = ROOT/'tools'/'make_test_saves.py'
    code = path.read_text()
    if code.count(before) != 1:
        raise AssertionError(f'mutant drift {kind}: {code.count(before)}')
    module = type(sys)('mutant_' + kind.replace('-', '_'))
    module.__file__ = str(path)
    exec(compile(code.replace(before, after), str(path), 'exec'), module.__dict__)
    if oracle == 'account':
        caught = not placeholder_oracle(module)
    elif oracle == 'manifest':
        caught = not manifest_oracle(module)
    else:
        try:
            caught = not refused(module, REFUSALS[oracle])
        except Exception:
            caught = True
    if not caught:
        raise AssertionError(f'mutant survived: {kind}')
    return kind


class Builder(unittest.TestCase):
    def test_container_matches_the_launcher(self):
        a = V['VectorA']
        self.assertEqual(builder.account_of(a), USER_A)
        self.assertEqual(builder.resign(a, USER_B), V['VectorB'])
        self.assertEqual(builder.resign(a, 'steam_9'), V['VectorShort'])
        self.assertEqual(builder.resign(V['VectorB'], USER_A), a)
        for cut in (0, 10, 30, len(a) - 1):
            with self.assertRaises(builder.Refused):
                builder.parse_checkpoint(a[:cut])
        with self.assertRaises(builder.Refused):
            builder.parse_checkpoint(V['VectorProfile'])

    def test_pack(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)/'deathstranding-coop-test-saves-0.1.0-alpha.1.zip'
            builder.build(source_tree(Path(d)/'src'), out, '0.1.0-alpha.1')
            with zipfile.ZipFile(out) as z:
                names = z.namelist()
                self.assertEqual(names, ['test-saves.json', 'ep1/manualsave0/checkpoint.dat', 'ep1/autosave2/checkpoint.dat',
                                         'late/quicksave1/checkpoint.dat'])
                self.assertTrue(all(i.compress_type == zipfile.ZIP_STORED for i in z.infolist()))
                m = json.loads(z.read('test-saves.json'))
                self.assertEqual((m['schema'], m['version']), (1, '0.1.0-alpha.1'))
                ep1, late = m['sets']
                self.assertEqual((ep1['id'], ep1['title'], ep1['description'], ep1['episode'], ep1['play_time'], ep1['source']),
                                 ('ep1', 'Episode one', 'Synthetic.\nTwo lines.', 1, '2 h', 'test'))
                self.assertEqual((late['play_time'], late['episode'], late['source']), ('80 h', 14, ''))
                self.assertEqual(ep1['files'][0]['path'], 'ep1/manualsave0/checkpoint.dat')   # continue first
                for s in m['sets']:
                    for f in s['files']:
                        data = z.read(f['path'])
                        self.assertEqual((hashlib.sha256(data).hexdigest(), len(data)), (f['sha256'], f['size']))
                        self.assertEqual(builder.account_of(data), builder.PLACEHOLDER)   # nobody's account
                self.assertEqual(builder.resign(z.read('ep1/manualsave0/checkpoint.dat'), USER_B), V['VectorB'])
            again = Path(d)/'again.zip'
            builder.build(Path(d)/'src', again, '0.1.0-alpha.1')
            self.assertEqual(out.read_bytes(), again.read_bytes(), 'the same input makes the same bytes')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(builder.main([str(Path(d)/'src'), str(Path(d)/'cli.zip'), '--version', 'x']), 0)

    def test_default_continue_is_newest(self):
        def newest(src):
            data = json.loads((src/'sets.json').read_text())
            del data['sets'][0]['continue']
            (src/'sets.json').write_text(json.dumps(data))
            os.utime(src/'ep1'/'manualsave0'/'checkpoint.dat', (1000, 1000))
            os.utime(src/'ep1'/'AutoSave2'/'checkpoint.dat', (2000, 2000))
        with tempfile.TemporaryDirectory() as d:
            sets = builder.build(source_tree(Path(d)/'src', newest), Path(d)/'o.zip', 'v')
            self.assertEqual(sets[0]['files'][0]['folder'], 'autosave2')

    def test_refusals(self):
        for name, change in REFUSALS.items():
            with self.subTest(name):
                self.assertTrue(refused(builder, change), name)
        with tempfile.TemporaryDirectory() as d:
            src = source_tree(Path(d)/'src')
            with self.assertRaises(builder.Refused):
                builder.build(src, Path(d)/'o.zip', '')
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(builder.main([str(Path(d)/'src'/'ep1'), str(Path(d)/'o.zip'), '--version', 'v']), 2)

    def test_release_script(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            src = source_tree(Path(d)/'src')
            env = dict(os.environ, TEST_SAVES_DIR=str(src), VERSION='v0.1.0-alpha.1')
            r = subprocess.run(['sh', str(ROOT/'tools'/'build_test_saves.sh'), str(Path(d)/'out')], env=env, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            pack = Path(d)/'out'/'deathstranding-coop-test-saves-0.1.0-alpha.1.zip'
            line = (Path(d)/'out'/(pack.name + '.sha256')).read_text()
            self.assertEqual(line, hashlib.sha256(pack.read_bytes()).hexdigest() + '  ' + pack.name + '\n')
            env['TEST_SAVES_DIR'] = str(ROOT/'tools')   # saves inside the repository: refused
            r = subprocess.run(['sh', str(ROOT/'tools'/'build_test_saves.sh'), str(Path(d)/'out2')], env=env, capture_output=True, text=True)
            self.assertEqual(r.returncode, 2)
            self.assertFalse((Path(d)/'out2').exists())

    def test_mutants(self):
        self.assertEqual(set(parallel(mutant, list(MUTANTS))), set(MUTANTS))


if __name__ == '__main__':
    unittest.main()
