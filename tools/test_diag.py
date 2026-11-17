#!/usr/bin/env python3
"""Game-free privacy, manifest, chain, merge and crash-reader tests + parallel mutants.

The table exercised here is embedded unchanged with diag.ps1. To also run the
exporter end to end on a Windows host: python tools/test_diag.py --powershell
(or --powershell pwsh on a host that has PowerShell 7).
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

import diag_report as d
from diag_rules import RULES, Redactor, select_logs
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mod'))
from mutants import parallel

ROOT = Path(__file__).resolve().parents[1]
VALUES = dict(profile=r'C:\Users\FixtureUser', user='FixtureUser', machine='FixtureComputer')
CASES = {
    'profile': (r'C:\Users\FixtureUser\game', r'C:\Users\FixtureUser'),
    'user': ('hello FixtureUser!', 'FixtureUser'),
    'machine': ('computer=FixtureComputer', 'FixtureComputer'),
    'user_path': (r'D:\Users\AnotherPerson\game', 'AnotherPerson'),
    'ip': ('peer 198.51.100.42:49100', '198.51.100.42'),
    'steam': ('76561198012345678 steam_123456', '76561198012345678'),
    'mac': ('aa:bb:cc:dd:ee:ff', 'aa:bb:cc:dd:ee:ff'),
    'email': ('someone@example.invalid', 'someone@example.invalid'),
    'tailnet': ('node.tail.ts.net', 'node.tail.ts.net'),
    'secret': ('"relay_key":"fixtureRelaySecret"', 'fixtureRelaySecret'),
    'authorization': ('Authorization: Bearer fixtureBearerSecret', 'fixtureBearerSecret'),
    'invite': ('DSG-ABCDEF123456', 'DSG-ABCDEF123456'),
    'world_key': ('http://example.invalid/w0123abcd/auth/ds', 'w0123abcd'),
    'ticket': ('/t/fixtureTicket/auth/ds', 'fixtureTicket'),
}


def session(texts, base=1761570000000):
    data, prev = b'', d.FNV_BASIS
    for n, text in enumerate(texts):
        line, prev = d.format_line(prev, n, base + n * 100, text.encode())
        data += line
    return data


def synthetic_dump():
    """A minimal x64 minidump built from public structs, with no game memory."""
    import struct
    data = bytearray(800)
    struct.pack_into('<4sIIIIIQ', data, 0, b'MDMP', 0, 3, 32, 0, 0, 0)
    for index, (kind, size, offset) in enumerate(((7, 2, 68), (4, 112, 72), (6, 168, 184))):
        struct.pack_into('<III', data, 32 + index*12, kind, size, offset)
    struct.pack_into('<H', data, 68, 9)
    struct.pack_into('<I', data, 72, 1)
    struct.pack_into('<QIIII', data, 76, 0x180000000, 0x5000, 0, 1234, 700)
    name = 'avrt.dll'.encode('utf-16le')
    struct.pack_into('<I', data, 700, len(name)); data[704:704+len(name)] = name
    struct.pack_into('<II', data, 184, 42, 0)
    struct.pack_into('<IIQQI', data, 192, 0xc0000005, 0, 0, 0x180001010, 0)
    struct.pack_into('<II', data, 344, 256, 352)
    struct.pack_into('<Q', data, 352 + 0xf8, 0x180001010)
    return bytes(data)


def bundle(path, files, chains=None):
    entries = []
    for name, data in files.items():
        entry = dict(file=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        if chains and name in chains:
            entry['chain'] = chains[name]
        entries.append(entry)
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
        archive.writestr('manifest.json', json.dumps(dict(format=1, files=entries, problems=[])))


def privacy_oracle(redactor, case):
    original, private = case
    assert private.lower() not in redactor.text(original).lower(), private
    for encoding in ('ascii', 'utf-16le'):
        raw = b'\x00\xff' + original.encode(encoding) + b'\x00\xff'
        masked = redactor.binary(raw)
        assert len(masked) == len(raw), 'dump byte offsets changed'
        assert private.encode(encoding).lower() not in masked.lower(), 'dump leaked ' + private
        assert masked[:2] == raw[:2] and masked[-2:] == raw[-2:]


def mutant(rule_id):
    # Remove precisely one rule; the unchanged oracle must reject it. Use an
    # isolated policy for each rule so overlapping masks cannot hide a survivor.
    policy = [r for r in RULES['rules'] if r['id'] == rule_id]
    r = Redactor(**VALUES, rules=policy)
    privacy_oracle(r, CASES[rule_id])
    try:
        privacy_oracle(Redactor(**VALUES, rules=[]), CASES[rule_id])
    except AssertionError:
        return rule_id
    raise AssertionError('privacy mutant survived: ' + rule_id)


def implementation_mutant(kind):
    """Run the same rejection oracle against one deliberately broken implementation."""
    path = ROOT/'tools'/('diag_rules.py' if kind in ('budget', 'dump') else 'diag_report.py')
    code = path.read_text()
    replacements = {
        'manifest': ('    def check_manifest(self):\n', '    def check_manifest(self):\n        return\n'),
        'chain': ("if m[3] != b'%08x' % prev:", 'if False:'),
        'budget': ('if size > budget:', 'if False:'),
        'dump': ('    def binary(self, data):\n', '    def binary(self, data):\n        return data\n'),
        'alignment': ('if len(times) == 1}', 'if len(times) >= 1}'),
        'archive': ("if name.startswith('/') or '..' in name.split('/') or name in self.files:", 'if False:')
    }
    before, after = replacements[kind]
    assert before in code, 'mutant did not change implementation'
    mutated = dict(__file__=str(path), __name__='mutant_'+kind)
    exec(compile(code.replace(before, after, 1), str(path), 'exec'), mutated)
    try:
        if kind == 'chain':
            damaged = session(['hello']).replace(b'hello', b'jello')
            assert mutated['parse_log'](damaged)[1], 'edited chain accepted'
        elif kind == 'budget':
            selected, _skipped = mutated['select_logs']([('crash.dmp', 1000, 2), ('new.log', 6, 1)], 10)
            assert selected == ['new.log'], 'size limit bypassed'
        elif kind == 'dump':
            privacy_oracle(mutated['Redactor'](**VALUES), CASES['steam'])
        elif kind == 'alignment':
            reports = [dict(source='a', marks=[dict(number=1, utc_ms=100)]), dict(source='b', marks=[dict(number=1, utc_ms=500), dict(number=1, utc_ms=600)])]
            assert mutated['alignment'](reports)[0]['b'] == 0, 'ambiguous marker aligned'
        else:
            with tempfile.TemporaryDirectory() as folder:
                zip_path = Path(folder)/'fixture.zip'
                bundle(zip_path, {'report.txt': b'original'})
                with zipfile.ZipFile(zip_path) as archive:
                    contents = {info.filename: archive.read(info) for info in archive.infolist()}
                contents['report.txt'] = b'tampered'
                if kind == 'archive': contents['../escape.txt'] = b'bad'
                with zipfile.ZipFile(zip_path, 'w') as archive:
                    for name, data in contents.items(): archive.writestr(name, data)
                if kind == 'manifest':
                    assert mutated['Source'](zip_path).problems, 'tampered manifest accepted'
                else:
                    refused = False
                    try: mutated['Source'](zip_path)
                    except ValueError: refused = True
                    assert refused, 'unsafe zip name accepted'
    except AssertionError:
        return kind
    raise AssertionError('implementation mutant survived: '+kind)


class Diagnostics(unittest.TestCase):
    def test_shared_redaction_policy(self):
        redactor = Redactor(**VALUES)
        for case in CASES.values():
            privacy_oracle(redactor, case)
        self.assertEqual(json.loads(redactor.text('{"relay_key":"fixtureRelaySecret"}'))['relay_key'], '<secret>')
        self.assertNotIn('123456', redactor.text('steam_123456'))
        self.assertEqual(redactor.text('198.51.100.42 198.51.100.42'), 'ip-1 ip-1')
        # Loopback is also masked; no special cases leak addresses.
        self.assertRegex(redactor.text('127.0.0.1'), '^ip-')
        self.assertNotIn('secretValue', Redactor(secrets=['secretValue']).text('arbitrary secretValue log'))
        # Non-ASCII UTF-16 names do not corrupt adjacent binary bytes.
        raw = b'\xfe' + 'Example\u00f1'.encode('utf-16le') + b'\xff'
        masked = Redactor(user='Example\u00f1').binary(raw)
        self.assertEqual(len(raw), len(masked))
        self.assertNotIn('Example\u00f1'.encode('utf-16le'), masked)
        raw8 = b'prefix ' + 'Example\u00f1'.encode('utf-8')
        self.assertNotIn('Example\u00f1'.encode('utf-8'), Redactor(user='Example\u00f1').binary(raw8))
        dump = synthetic_dump() + b'\x00FixtureUser\x00' + 'FixtureComputer'.encode('utf-16le')
        masked_dump = redactor.binary(dump)
        self.assertEqual(len(masked_dump), len(dump))
        self.assertEqual(d.minidump.Minidump(masked_dump).exception['code'], 0xc0000005)
        self.assertNotIn(b'FixtureUser', masked_dump)
        self.assertNotIn('FixtureComputer'.encode('utf-16le'), masked_dump)

    def test_privacy_mutants(self):
        self.assertEqual(set(parallel(mutant, CASES)), set(CASES))

    def test_implementation_mutants(self):
        kinds = ('manifest', 'chain', 'budget', 'dump', 'alignment', 'archive')
        self.assertEqual(set(parallel(implementation_mutant, kinds)), set(kinds))

    def test_budget(self):
        files = [('older.log', 5, 1), ('new.log', 6, 3), ('crash.dmp', 1000, 4), ('middle.log', 4, 2)]
        selected, skipped = select_logs(files, 10)
        self.assertEqual(selected, ['new.log', 'middle.log'])
        self.assertEqual(skipped, ['crash.dmp', 'older.log'])
        self.assertEqual(select_logs(files, 0)[0], [])
        self.assertEqual(RULES['default_max_log_mb'], 60)

    def test_chain(self):
        raw = session(['hello', 'BUGMARK 1', 'session end: clean exit'])
        self.assertIsNone(d.parse_log(raw)[1])
        self.assertEqual(d.format_line(d.FNV_BASIS, 0, 1, b'a\nb')[0].count(b'\n'), 1)
        for bad in (raw.replace(b'hello', b'jello'), b'\n'.join(raw.split(b'\n')[1:])):
            self.assertIsNotNone(d.parse_log(bad)[1])
        self.assertIsNone(d.parse_log(raw + b'3 1761 half-written')[1])
        self.assertEqual(d.format_line(d.FNV_BASIS, 0, 1, b'hello')[0], b'0 1 d85c7a4c hello\n')

    def test_manifest_and_reader(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'host.zip'
            name = 'logs/20261117-000000_game_p1.log'
            original = session(['BUGMARK 1', 'crash: unhandled exception 0xc0000005 at avrt.dll+0x1010'])
            redacted = original.replace(b'avrt.dll', b'<module>')
            files = {name: redacted, name[:-4]+'.crash': b'crashed', 'README.txt': b'note'}
            chain = {'status': 'ok', 'clean_exit': False}
            bundle(path, files, {name: chain})
            source = d.Source(path)
            self.assertEqual(source.problems, [])
            self.assertEqual(source.chain_info(name), chain)
            # Redacted logs trust the recorded original chain; rechecking them would give a false alarm.
            self.assertIsNotNone(d.parse_log(redacted)[1])
            self.assertEqual(len(d.summarise(source)['crashes']), 1)
            with zipfile.ZipFile(path) as archive:
                contents = {i.filename: archive.read(i) for i in archive.infolist()}
            for kind in ('changed', 'missing', 'extra'):
                altered = dict(contents)
                if kind == 'changed': altered[name] += b'tampered'
                if kind == 'missing': del altered[name]
                if kind == 'extra': altered['unlisted.txt'] = b'new'
                with zipfile.ZipFile(path, 'w') as archive:
                    for key, value in altered.items(): archive.writestr(key, value)
                self.assertTrue(d.Source(path).problems, kind)
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('../outside.txt', b'bad')
            with self.assertRaises(ValueError): d.Source(path)

    def test_minidump_and_build_mapping(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'dump.zip'
            stem = 'logs/20261117-000000_game_p1'
            bundle(path, {stem+'.dmp': synthetic_dump()})
            source = d.Source(path)
            symbols = d.map_symbols.SymbolMap('  0x0000000180000000 __image_base__ = 0x180000000\n .text  0x0000000180001000 0x100 crash.o\n  0x0000000180001000 crash_handler\n')
            crash = d.crash_info(source, stem, '', symbols, 1234)
            self.assertEqual(crash['module'], 'avrt.dll')
            self.assertEqual(crash['offset'], 0x1010)
            self.assertEqual(crash['code'], '0xc0000005')
            self.assertIn('crash_handler', crash['symbol'])
            mismatch = d.crash_info(source, stem, '', symbols, 1235)
            self.assertIn('map_warning', mismatch)
            self.assertNotIn('symbol', mismatch)
            old = d.crash_info(source, 'legacy/crash-old', 'CRASH\tunhandled c0000005 access violation at mod.dll+0x10\ttid=42')
            self.assertEqual(old['module'], 'mod.dll')
            self.assertEqual(old['code'], '0xc0000005')

    def test_merge_markers_crashes_and_map(self):
        with tempfile.TemporaryDirectory() as folder:
            sources, reports = [], []
            symbol_map = d.map_symbols.SymbolMap('  0x0000000180000000 __image_base__ = 0x180000000\n .text  0x0000000180001000 0x100 crash.o\n  0x0000000180001000 crash_handler\n')
            for who, base in (('host', 1761570000000), ('guest', 1761570005000)):
                path = Path(folder) / (who + '.zip')
                name = 'logs/20261117-000000_game_p1.log'
                raw = session(['BUGMARK 1', 'crash: unhandled exception 0xc0000005 at avrt.dll+0x1010'], base)
                bundle(path, {name: raw, name[:-4]+'.dmp': b'not a dump', name[:-4]+'.flight-1.txt': b'last minute'}, {name: {'status': 'ok'}})
                sources.append(d.Source(path))
                reports.append(d.summarise(sources[-1], symbol_map))
            self.assertEqual(d.alignment(reports)[0]['guest.zip'], -5000)
            crash = reports[0]['crashes'][0]
            self.assertEqual(crash['code'], '0xc0000005')
            self.assertIn('crash_handler', crash['symbol'])
            self.assertIn('dump_error', crash)
            self.assertEqual(len(reports[0]['flight']), 1)
            output = Path(folder) / 'timeline.txt'
            self.assertEqual(d.timeline(sources, reports, output, True), 4)
            rows = [line for line in output.read_text().splitlines() if not line.startswith('#')]
            self.assertEqual(rows[0].split()[0], rows[1].split()[0])
            self.assertIn('[host.zip logs/', output.read_text())
            reports[1]['marks'] *= 2
            offsets, warnings = d.alignment(reports)
            self.assertEqual(offsets['guest.zip'], 0)
            self.assertTrue(warnings)
            result = subprocess.run([sys.executable, str(ROOT/'tools/diag_report.py'), str(Path(folder)/'host.zip'), '--json'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)[0]['crashes'][0]['module'], 'avrt.dll')

    def test_embedded_exporter_wiring(self):
        script = (ROOT/'dist/diag.ps1').read_text()
        rc = (ROOT/'launcher/launcher.rc').read_text()
        self.assertIn('diag-rules.json', script)
        self.assertIn('"../dist/diag.ps1"', rc)
        self.assertIn('"../dist/diag-rules.json"', rc)
        self.assertIn('$script:budget -= $bytes.Length', script)
        self.assertIn('[DsDiag]::Chain($bytes', script)
        self.assertIn('Get-FileHash', script)
        main = (ROOT/'launcher/main.cpp').read_text()
        self.assertIn('L"--diagnostics-zip"', main)
        self.assertIn('L"--diagnostics"', main)


def windows_fixture(powershell):
    """Run the real exporter against synthetic files; no game or hardware queries."""
    with tempfile.TemporaryDirectory(prefix='ds-diag-') as folder:
        root = Path(folder)
        data, logs, game = root/'data', root/'data/logs', root/'game'
        logs.mkdir(parents=True); game.mkdir()
        (game/'avrt.dll').write_bytes(b'synthetic proxy fixture')
        (game/'coop-mod.receipt.json').write_text('{"schema":1,"files":[]}')
        (game/'ds.exe').write_bytes(b'synthetic exe fixture; never run')
        (data/'player.key').write_bytes(b'private fixture key; must not collect')
        (data/'save-slot-0.dat').write_bytes(b'private fixture save; must not collect')
        old = data/'redirect-data/mod'; old.mkdir(parents=True)
        fixture_cases = dict(CASES)
        import getpass, socket
        user = os.environ.get('USERNAME') or getpass.getuser()
        machine = os.environ.get('COMPUTERNAME') or socket.gethostname()
        profile = os.environ.get('USERPROFILE') or str(Path.home())
        fixture_cases.update(user=('hello '+user+'!', user), machine=('machine '+machine, machine), profile=(profile+'/game', profile))
        text = ' '.join(v[0] for v in fixture_cases.values())
        raw = session([text, 'x'*600000])
        log = logs/'20261117-000000_game_p1.log'; log.write_bytes(raw)
        legacy = old/'mod-123-old.log'; legacy.write_text(text + 'x'*600000)
        dump = logs/'20261117-000001_game_p2.dmp'; dump.write_bytes(b'x' * (2*1024*1024))
        # Distinct mtimes make the strict newest-first expectation portable.
        os.utime(legacy, (100,100)); os.utime(log, (200,200)); os.utime(dump, (300,300))
        (data/'launcher.ini').write_text('session=fixtureSecret\n')
        (old/'mod.json').write_text('{"relay_key":"fixtureSecret"}')
        report = root/'report.txt'; report.write_text(text)
        result = root/'result.txt'
        cmd = [powershell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT/'dist/diag.ps1'), '-DataDir', str(data), '-GameDir', str(game), '-OutDir', str(root), '-ReportFile', str(report), '-ResultFile', str(result), '-MaxLogMB', '1', '-NoOpen', '-Fixture']
        ran = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        assert ran.returncode == 0, ran.stdout + ran.stderr
        outcome = result.read_text(encoding='utf-8-sig')
        assert outcome.startswith('OK '), outcome
        source = d.Source(outcome[3:])
        assert all(p.startswith('collection: size limit:') for p in source.problems), source.problems
        assert source.manifest['collected_log_bytes'] == len(raw)
        assert 'logs/' + log.name in source.files
        assert not any(name.endswith('.dmp') or name.startswith('legacy/') for name in source.files)
        assert source.chain_info('logs/' + log.name)['status'] == 'ok'
        assert not any(name.endswith(('.dll', '.exe', '.key', '.dat')) for name in source.files)
        listing = source.files['game-folder.txt'].decode()
        assert hashlib.sha256((game/'avrt.dll').read_bytes()).hexdigest() in listing
        assert hashlib.sha256((game/'coop-mod.receipt.json').read_bytes()).hexdigest() in listing
        for name, content in source.files.items():
            for _value, private in fixture_cases.values():
                assert private.encode() not in content, (name, private)
        # Second export: both locations, UTF-16 legacy input, dump offset preservation.
        dump.write_bytes(b'\x00' + text.encode('utf-16le') + b'\x00')
        legacy.write_bytes(('\ufeff'+text).encode('utf-16le'))
        raw_size = dump.stat().st_size
        ran = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        assert ran.returncode == 0, ran.stdout + ran.stderr
        source = d.Source(result.read_text(encoding='utf-8-sig')[3:])
        assert not source.problems, source.problems
        assert any(name.startswith('legacy/') for name in source.files)
        assert len(source.files['logs/'+dump.name]) == raw_size
        for content in source.files.values():
            for _value, private in fixture_cases.values():
                assert private.encode() not in content and private.encode('utf-16le') not in content, private
        print('Windows exporter fixture passed (redaction, manifest, chain, limit, legacy and dumps).')


if __name__ == '__main__':
    if '--powershell' in sys.argv:
        index = sys.argv.index('--powershell')
        exe = sys.argv[index+1] if index+1 < len(sys.argv) else 'powershell.exe'
        windows_fixture(exe)
    else:
        unittest.main()
