"""coop-partner --carry end to end on loopback, no game: one partner carries, another shows it.

The carrying side (slot 12) sends its summary once connected. The first receiver (slot 13) stops after 3 s; a second
one starts and must be told again, exactly once, without the carrier changing anything. 4 s after that connection
the carrier puts everything down and the second receiver must show nothing.
"""
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
PARTNER = ROOT / 'build' / 'coop-partner'


def start(*args):
    return subprocess.Popen([str(PARTNER), '--session', '4243', *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    if not PARTNER.exists():
        sys.exit('build/coop-partner is missing (make build/coop-partner)')
    carrier = start('--slot', '12', '--peer-slot', '13', '--role', 'host', '--seconds', '14',
                    '--carry', '125983117:1:2,7:7', '--carry-drop-after', '4')
    first = start('--slot', '13', '--peer-slot', '12', '--role', 'client', '--seconds', '3')
    out_first = first.communicate(timeout=20)[0]
    time.sleep(1)
    second = start('--slot', '13', '--peer-slot', '12', '--role', 'client', '--seconds', '9')
    out_second = second.communicate(timeout=20)[0]
    out_carrier = carrier.communicate(timeout=20)[0]
    shown = 'CARRY the porter carries 2 entries; 125983117 BackPack stack 0 level 0 x2; 7 RightHand stack 0 level 0 x1'
    lines = lambda text: [l for l in text.splitlines() if l.startswith('CARRY')]
    problems = []
    if lines(out_first) != [shown]:
        problems.append(('first receiver', lines(out_first)))
    if lines(out_second) != [shown, 'CARRY the porter carries nothing']:
        problems.append(('second receiver (after a restart)', lines(out_second)))
    if 'CARRY put everything down' not in out_carrier:
        problems.append(('carrier', lines(out_carrier)))
    if problems:
        for p in problems:
            print('carry oracle:', p, file=sys.stderr)
        sys.exit(1)
    print('coop-partner carry checks passed')


main()
