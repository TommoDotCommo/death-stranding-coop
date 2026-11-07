#!/usr/bin/env python3
"""Runs crash_record_probe.exe (native Windows, game-free) through every case and checks the recorder's files.

Runs from WSL (the .exe starts through interop; folders are passed as Windows paths with wslpath -w) or from native
Windows Python. Each case gets a NEW empty folder under --work. Exit 0 = every check passed. A mutant build of the
probe must make this script fail.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def windows_path(path):
    if os.name == 'nt':
        return str(path)
    return subprocess.run(['wslpath', '-w', str(path)], check=True, capture_output=True, text=True).stdout.strip()


def run(exe, folder, mode, timeout=60):
    folder.mkdir(parents=True)
    r = subprocess.run([str(exe), windows_path(folder), mode], capture_output=True, text=True, timeout=timeout)
    lines = [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
    logs = sorted(folder.glob('crash-*.log'))
    text = logs[0].read_bytes().decode('utf-16-le') if logs else ''
    return r, lines, logs, text


def records(text, kind):
    return [line.split('\t') for line in text.splitlines() if line.startswith(kind + '\t')]


def frames(fields):
    return re.findall(r'#\d+ (\S+)', fields[1])


def offset_in(location, module, start, length):
    prefix = module + '+0x'
    return location.startswith(prefix) and start <= int(location[len(prefix):], 16) < start + length


class Failure(Exception):
    pass


def expect(ok, what):
    if not ok:
        raise Failure(what)


def check_map(text, exe_name, when):
    maps = records(text, 'CRASH_MODULE')
    names = {m[1].casefold() for m in maps if m[2].endswith('(%s)' % when)}
    expect(exe_name.casefold() in names and 'ntdll.dll' in names, f'{when} module map names the exe and ntdll')
    for m in maps:
        expect(re.fullmatch(r'base=0x[0-9a-f]{16} size=0x[0-9a-f]+ \((startup|first record)\)', m[2]), 'module line has base and size')
    expect(any(r[2] == when for r in records(text, 'CRASH_MODULES')), f'{when} module count line')


def case_off(exe, work):
    r, lines, logs, _ = run(exe, work / 'off', 'off')
    expect(r.returncode == 0 and lines[-1].get('continued') and lines[-1]['first'] == 42, f'off: process continues ({r.returncode})')
    expect(not logs, 'off: no recorder log')


def case_handled(exe, work):
    r, lines, logs, text = run(exe, work / 'handled', 'handled')
    expect(r.returncode == 0, f'handled: process continues ({r.returncode}: {r.stderr.strip()})')
    info, result = lines[0], lines[-1]
    expect(result.get('continued') and result['first'] == 42 and result['second'] == 42, 'handled: the game handler recovered both faults')
    expect(result['bad_recovered'] == 40, 'handled: the 40 bad calls returned through the game handler')
    expect(len(logs) == 1, 'handled: one recorder log')
    expect(records(text, 'CRASH_RECORD'), 'handled: header line')
    check_map(text, exe.name, 'startup')
    check_map(text, exe.name, 'first record')
    crashes = records(text, 'CRASH')
    expect(all(c[1].startswith('first chance ') for c in crashes), 'handled: every record says first chance')
    expect(not any('unhandled' in c[1] for c in crashes), 'handled: nothing unhandled')
    fault = exe.name + '+0x%x' % info['fault_read']
    at_fault = [c for c in crashes if c[1].endswith(' at ' + fault)]
    expect(len(at_fault) == 1, f'handled: exactly one record at {fault} (dedupe of the repeat), got {len(at_fault)}')
    expect('0xc0000005 access violation read 0x0000000000000000' in at_fault[0][1], 'handled: code, kind and address')
    expect('repeats=' in crashes[1][2] and 'repeats=1' in crashes[1][2], 'handled: the repeat was counted')
    expect(len(crashes) == 32, f'handled: capped at 32 records, got {len(crashes)}')
    expect(records(text, 'CRASH_CAP'), 'handled: cap noted')
    stacks = records(text, 'CRASH_STACK')
    expect(len(stacks) == len(crashes), 'handled: one stack per record')
    first = frames(stacks[0])
    expect(first[0] == fault, 'handled: frame 0 is the faulting instruction')
    expect(offset_in(first[1], exe.name, info['do_handled'], 0x20), 'handled: frame 1 returns into do_handled')
    expect(any(f.casefold().startswith('ntdll.dll+0x') for f in first), 'handled: the walk reaches ntdll')
    expect(f'tid={info["main_tid"]} main' in stacks[0][2], 'handled: thread id of the main thread')


def case_handled_dump(exe, work):
    folder = work / 'handled-dump'
    r, lines, logs, text = run(exe, folder, 'handled-dump')
    expect(r.returncode == 0 and lines[-1].get('continued') and lines[-1]['dump_ready'], f'handled-dump: process continues with dbghelp ready ({r.returncode})')
    dumps = list(folder.glob('crash-*.dmp'))
    expect(len(dumps) == 1 and dumps[0].read_bytes()[:4] == b'MDMP', 'handled-dump: level 2 writes the dump at the first record')
    expect(any(d[2].startswith('written') for d in records(text, 'CRASH_DUMP')), 'handled-dump: dump line')


def case_unhandled(exe, work):
    folder = work / 'unhandled'
    r, lines, logs, text = run(exe, folder, 'unhandled')
    info = lines[0]
    expect(r.returncode != 0, 'unhandled: the process ends')
    expect(len(logs) == 1, 'unhandled: one recorder log')
    fault = exe.name + '+0x%x' % info['fault_write']
    crashes = records(text, 'CRASH')
    stages = [c[1].split(' 0x')[0] for c in crashes if c[1].endswith(' at ' + fault)]
    expect(stages == ['first chance', 'unhandled'], f'unhandled: first-chance then unhandled record, got {stages}')
    expect(' write 0x0000000000000000 ' in crashes[-1][1], 'unhandled: write to null')
    check_map(text, exe.name, 'first record')
    dumps = list(folder.glob('crash-*.dmp'))
    expect(len(dumps) == 1 and dumps[0].stat().st_size > 1024 and dumps[0].read_bytes()[:4] == b'MDMP', 'unhandled: minidump present')
    expect(any(d[2].startswith('written') for d in records(text, 'CRASH_DUMP')), 'unhandled: dump line')
    marker = folder / 'game-filter.txt'
    expect(marker.exists(), "unhandled: the game's own filter still ran after the recorder")


def case_hang(exe, work):
    r, lines, logs, text = run(exe, work / 'hang', 'hang')
    info = lines[0]
    expect(r.returncode == 0 and lines[-1].get('continued'), f'hang: process continues ({r.returncode})')
    hangs = records(text, 'HANG')
    expect(len(hangs) == 1 and 'stack follows' in hangs[0][2], 'hang: one hang note with a stack')
    stacks = records(text, 'HANG_STACK')
    expect(len(stacks) == 1, 'hang: one stack')
    expect(f'tid={info["main_tid"]} main' in stacks[0][2], 'hang: the main thread was sampled')
    expect(any(offset_in(f, exe.name, info['hang_here'], 0x20) for f in frames(stacks[0])),
           'hang: the stack shows where the main thread waits (hang_here)')
    expect(records(text, 'HANG_END'), 'hang: the pump resumed and was noted')


def case_research(exe, work, research):
    r, lines, logs, text = run(exe, work / 'research', 'research')
    info = lines[0]
    expect(r.returncode == 0 and lines[-1].get('continued'), f'research: process continues ({r.returncode})')
    notes = records(text, 'CRASH_RESEARCH')
    divides = [c for c in records(text, 'CRASH') if '0xc0000094 integer divide by zero' in c[1]]
    if not research:
        expect(not notes and not divides and lines[-1]['command_left'], 'research: a default build ignores crash.fault')
        return
    expect(len(notes) == 3 and 'ignored' in notes[0][2] and 'starting' in notes[1][2] and 'recovered' in notes[2][1],
           f'research: wrong stamp ignored, right stamp starts one recovered fault ({notes})')
    expect(len(divides) == 1 and divides[0][1].startswith('first chance ') and f'tid={info["main_tid"]} ' not in divides[0][2] + ' ',
           'research: one first-chance divide record on the research thread')


CASES = {'off': case_off, 'handled': case_handled, 'handled-dump': case_handled_dump,
         'unhandled': case_unhandled, 'hang': case_hang}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='folder for the case folders (emptied first)')
    parser.add_argument('--case', choices=sorted(CASES) + ['research'], action='append')
    parser.add_argument('--research', action='store_true', help='the exe is a CRASH_RECORD_RESEARCH build')
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = args.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    try:
        for name in args.case or list(CASES) + ['research']:
            if name == 'research':
                case_research(exe, work, args.research)
            else:
                CASES[name](exe, work)
            print(f'crash record {name}: ok')
    except (Failure, IndexError, KeyError, ValueError, subprocess.TimeoutExpired) as error:
        print(f'crash record FAILED: {type(error).__name__}: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
