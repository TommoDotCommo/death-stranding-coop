"""F2 through the real pad script and reliable partner stream, using fake game memory."""
import ctypes
import os
from pathlib import Path
import re
import tempfile

from test_others_partner import Game, World, oracle


def verify_chain(path):
    chain = 2166136261
    for expected, line in enumerate(path.read_text(encoding='utf-8').splitlines()):
        number, utc, recorded, text = line.split(' ', 3)
        oracle(int(number) == expected and re.fullmatch('[0-9a-f]{8}', recorded), 'chain line format')
        for byte in f'{number} {utc} {text}'.encode():
            chain = ((chain ^ byte) * 16777619) & 0xffffffff
        oracle(chain == int(recorded, 16), 'continued FNV chain')


def main():
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        before = os.environ.get('XDG_STATE_HOME')
        games = []
        try:
            logs = []
            for name, slot in [('Ann', 0), ('Bob', 1)]:
                os.environ['XDG_STATE_HOME'] = str(root / ('state-' + name))
                logs.append(root / ('state-' + name) / 'DeathStrandingCoop' / 'logs')
                games.append(Game(folder, 'build/fake.so', name, (10 + slot * 4, 20, 30),
                                  partner=('host' if slot == 0 else 'client', slot), others=0,
                                  extra={'virtual_pad': True}))
            a, b = games
            link = World(games, a, b)
            oracle(link.run(10, lambda: all(g.status()['connected'] for g in games)), 'partners connect')
            # Receiving a bug mark needs no partner body, including in an event.
            b.lib.mod_test_set_event(1)
            a.lib.mod_test_run_stamp.restype = ctypes.c_char_p
            stamp = a.lib.mod_test_run_stamp().decode()
            (a.root / 'pad.script').write_text(f'pad {stamp}\nkey F2 500\nwait 250\nkey F2 500\nwait 250\n')
            link.run(2.5)
            for directory in logs:
                files = list(directory.glob('*_game_p*.log'))
                oracle(len(files) == 1, 'one game session log')
                text = files[0].read_text()
                marks = re.findall(r'^\d+ \d+ [0-9a-f]{8} BUGMARK (\d+)$', text, re.M)
                oracle(marks == ['1', '2'], ('one mark per press, same partner numbers, no echo', marks))
                for mark in marks:
                    flight = files[0].with_suffix(f'.flight-{mark}.txt').read_text()
                    oracle(f'BUGMARK {mark}\n' in flight and 'STATE ' in flight, 'flight snapshot')
        finally:
            for game in games:
                game.close()
            if before is None:
                os.environ.pop('XDG_STATE_HOME', None)
            else:
                os.environ['XDG_STATE_HOME'] = before
        for directory in logs:
            for path in directory.glob('*.log'):
                oracle('session end: clean exit' in path.read_text(), 'explicit shutdown clean exit')
                verify_chain(path)
    print('F2 pad, partner markers, flight files and chains: passed')


if __name__ == '__main__':
    main()
