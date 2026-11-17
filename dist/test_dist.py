#!/usr/bin/env python3
"""Game-free tests of the one-file Setup's portable core (setup-core.h, sha256.h), natively on Linux, with the mutants
that must fail them; and the Setup scripts' fixed facts. Run by `make -C dist test`."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
sys.path.insert(0, str(HERE.parent / 'mod'))
import mutants  # noqa: E402  the shared side-by-side runner
import payload  # noqa: E402

MUTANTS = ['NO_VERIFY', 'DOTDOT', 'DUPLICATES', 'TRAILING']


def build(work, macro=None):
    exe = Path(work) / ('setup-core-test' + ('-' + macro if macro else ''))
    subprocess.run(['cc', '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror'] + (['-DSC_MUTANT_' + macro] if macro else []) +
                   ['-I' + str(HERE), str(HERE / 'setup_core_test.c'), '-o', str(exe)], check=True)
    return exe


def oracle(exe, work):
    """Returns None when every check holds, else the first failed check."""
    work = Path(work)

    def run(*args):
        return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True).stdout.split('\n')

    def blob(name, data):
        path = work / name
        path.write_bytes(data)
        return path
    # SHA-256 against the standard vectors and Python's
    vectors = {b'': 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
               b'abc': 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
               b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq': '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1'}
    vectors[b'a' * 1000000] = hashlib.sha256(b'a' * 1000000).hexdigest()
    for i in (55, 56, 63, 64, 65, 119, 120):     # the padding boundaries
        data = bytes(x % 251 for x in range(i))
        vectors[data] = hashlib.sha256(data).hexdigest()
    for data, want in vectors.items():
        if run('sha', blob('v', data))[0] != want:
            return 'SHA-256 of %d bytes' % len(data)
    good = payload.pack([('installer-gui.ps1', b'gui'), ('redirect-data/mod/mod.dll', b'mod' * 1000), ('avrt.dll', b'')])
    out = run('parse', blob('good', good))
    if out[:4] != ['3', 'installer-gui.ps1 3', 'redirect-data/mod/mod.dll 3000', 'avrt.dll 0']:
        return 'a good payload parses: %r' % out[:4]
    digest = hashlib.sha256(good).hexdigest()
    if run('verify', blob('good', good), digest)[0] != '1':
        return 'a good payload passes its checksum'
    damaged = bytearray(good); damaged[-1] ^= 1
    if run('verify', blob('damaged', bytes(damaged)), digest)[0] != '0':
        return 'a damaged payload fails its checksum'

    def raw(entries, extra=b''):
        out = [b'MCP1', struct.pack('<I', len(entries))]
        for name, data in entries:
            out += [struct.pack('<H', len(name)), name, struct.pack('<I', len(data)), data]
        return b''.join(out) + extra
    bad = {'..': raw([(b'../avrt.dll', b'x')]), 'dot dir': raw([(b'a/./b', b'x')]), 'absolute': raw([(b'/avrt.dll', b'x')]),
           'backslash': raw([(b'a\\b', b'x')]), 'drive': raw([(b'C:x', b'x')]), 'trailing dot': raw([(b'avrt.dll.', b'x')]),
           'duplicate': raw([(b'avrt.dll', b'x'), (b'AVRT.dll', b'y')]), 'trailing bytes': raw([(b'avrt.dll', b'x')], b'!'),
           'size past the end': raw([(b'avrt.dll', b'x')])[:-1], 'magic': b'XCP1' + raw([(b'a', b'x')])[4:], 'empty': raw([])}
    for what, data in bad.items():
        if run('parse', blob('bad', data))[0] != '-1':
            return 'a malformed payload is refused: ' + what
    temp = 'C:/Users/u/AppData/Local/Temp'
    zips = {temp + '/Temp1_deathstranding-coop-v1.zip/deathstranding-coop-v1/DeathStrandingCoop-Setup.exe': '1',
            temp + '/x/DeathStrandingCoop-Setup.exe': '0', 'C:/Users/u/Downloads/deathstranding-coop-v1/DeathStrandingCoop-Setup.exe': '0',
            temp + '/DeathStrandingCoop-Setup.zip': '0'}
    for exe_path, want in zips.items():
        if run('zip', exe_path, temp + '/')[0] != want:
            return 'run from inside a zip: ' + exe_path
    return None


def main():
    with tempfile.TemporaryDirectory(prefix='ds-setup-core-') as work:
        failed = oracle(build(work), work)
        if failed:
            raise SystemExit('setup core oracle: ' + failed)

        def one(name):
            with tempfile.TemporaryDirectory(dir=work) as w:
                if oracle(build(w, name), w) is None:
                    raise SystemExit('SURVIVED setup core mutant ' + name)
            return name
        for name in mutants.parallel(one, MUTANTS):
            print('killed setup core', name)
    # The facts the scripts must keep: the launcher's protocol key, the verbs, the opt-in firewall.
    state = (HERE / 'install-state.ps1').read_text()
    gui = (HERE / 'installer-gui.ps1').read_text()
    verbs = (HERE / 'deathstranding-coop-setup.ps1').read_text()
    launcher = (HERE.parent / 'launcher' / 'launch_win.h').read_text()
    assert 'Software\\\\Classes\\\\deathstranding-coop' in launcher and "HKCU:\\Software\\Classes\\deathstranding-coop'" in state, 'one invite-link key'
    assert "'--register-protocol'" in state, 'invite links are registered by the launcher itself'
    assert '$optFirewall.Checked = $false' in gui, 'the firewall rule is opt-in'
    for verb in ("'status'", "'install'", "'uninstall'"):
        assert verb in verbs, 'verb ' + verb
    # The verbs reach the launcher unchanged, verb first (a Windows run once lost the verb this way).
    assert verbs.count('Invoke-DsSetup $words ') == 3, 'status, install and uninstall reach the launcher with the same words, verb first'
    print('PASS: SHA-256 vectors, Setup payload parsing and checksum, run-from-zip test, Setup script facts')


if __name__ == '__main__':
    main()
