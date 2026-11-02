#!/usr/bin/env python3
"""Read the opt-in proxy trace with VM_READ only; never inject, suspend or write.

Native Windows Python: --log <instance>/redirect-data/redirect-<pid>-<start>.log
The log identifies the buffer; process creation time prevents PID-reuse mistakes.
Output is one JSON snapshot. Missing, changing or unpublished records are explicit.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import struct
import sys

MAGIC = b'DSTHRD01'
CAPACITY, HEADER_SIZE, RECORD_SIZE, STACK_FRAMES = 2048, 80, 184, 16
BUFFER_SIZE = HEADER_SIZE + CAPACITY * RECORD_SIZE
HEADER = struct.Struct('<8sIIIIQQQIIIIQQ')
RECORD = struct.Struct('<IIQQIIIiQ16QII')


def identity_from_log(path):
    path = Path(path)
    match = re.fullmatch(r'redirect-([1-9][0-9]*)-([0-9a-fA-F]{16})\.log', path.name)
    if not match:
        raise ValueError('expected redirect-<pid>-<16-digit process start>.log')
    # This is an audit log, not an executable or a process-memory dump.
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('audit log exceeds 32 MiB reader cap')
    text = path.read_text(encoding='utf-16-le')
    entries = re.findall(r'^THREAD_TRACE\tv1 buffer=(0x[0-9a-f]+)\tbytes=([0-9]+);', text, re.M)
    if len(entries) != 1 or int(entries[0][1]) != BUFFER_SIZE:
        raise ValueError('missing, ambiguous or incompatible THREAD_TRACE record')
    return int(match[1]), int(match[2], 16), int(entries[0][0], 16)


def decode(first, second, pid, process_start):
    if len(first) != BUFFER_SIZE or len(second) != BUFFER_SIZE:
        raise ValueError('truncated trace buffer')
    a, b = HEADER.unpack_from(first), HEADER.unpack_from(second)
    if a[:10] != b[:10]:
        raise ValueError('trace header identity changed during read')
    magic, version, capacity, size, header, start, base, host_size, buffer_pid, pad, reserved, dropped, peek, get = b
    if (magic, version, capacity, size, header, start, buffer_pid, pad) != (
        MAGIC, 1, CAPACITY, RECORD_SIZE, HEADER_SIZE, process_start, pid, 0):
        raise ValueError('trace ABI or process identity mismatch')
    if not base or not host_size or base + host_size > (1 << 47):
        raise ValueError('invalid host image range')
    if reserved > CAPACITY or reserved % 2:
        raise ValueError('invalid reservation count')
    records, incomplete = [], []
    for index in range(reserved):
        offset = HEADER_SIZE + index * RECORD_SIZE
        x, y = first[offset:offset+RECORD_SIZE], second[offset:offset+RECORD_SIZE]
        if x != y or struct.unpack_from('<I', y)[0] == 0:
            incomplete.append(index)
            continue
        committed, tid, call, time_ms, api, phase, depth, result, caller, *tail = RECORD.unpack(y)
        frames, count, padding = tail[:STACK_FRAMES], tail[STACK_FRAMES], tail[STACK_FRAMES+1]
        if committed != 1 or not tid or not call or api > 1 or phase not in (1, 2) or not depth or count > STACK_FRAMES or padding:
            raise ValueError(f'invalid committed trace record {index}')
        if phase != 1 + index % 2:
            raise ValueError('trace pair phase order mismatch')
        def address(value):
            return {'va': hex(value), 'host_rva': hex(value-base) if base <= value < base+host_size else None}
        records.append({'index': index, 'tid': tid, 'call': call, 'time_ms': time_ms,
            'api': ('PeekMessageA', 'GetMessageA')[api], 'phase': ('enter', 'leave')[phase-1],
            'depth': depth, 'result': result if phase == 2 else None,
            'caller': address(caller), 'stack': [address(value) for value in frames[:count]]})
    # Match only complete pairs: a blocked GetMessage legitimately has an enter
    # record and no leave. Do not infer that it returned or that a thread stopped.
    by_index = {row['index']: row for row in records}
    for index in range(0, reserved, 2):
        enter, leave = by_index.get(index), by_index.get(index+1)
        if enter and leave and any(enter[key] != leave[key] for key in ('tid', 'call', 'api', 'depth', 'caller')):
            raise ValueError('trace pair identity mismatch')
        if enter and leave and leave['time_ms'] < enter['time_ms']:
            raise ValueError('trace clock regressed within a call')
    return {'pid': pid, 'process_start': hex(start), 'host_base': hex(base),
        'calls': {'PeekMessageA': peek, 'GetMessageA': get}, 'reserved': reserved,
        'dropped_pairs': dropped, 'capacity': CAPACITY, 'incomplete_records': incomplete,
        'observed_thread_ids': sorted({row['tid'] for row in records}),
        'safe_mutation_phase': 'unknown',
        'limitations': 'sampled call stacks, not frame or job synchronization; thread IDs may be reused',
        'records': records}


class WindowsReader:
    def __init__(self, pid, process_start):
        if os.name != 'nt':
            raise ValueError('requires native Windows Python')
        from ctypes import wintypes as w
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.api.OpenProcess.restype = w.HANDLE
        self.api.GetProcessTimes.argtypes = [w.HANDLE, *([ctypes.POINTER(w.FILETIME)] * 4)]
        self.api.GetProcessTimes.restype = w.BOOL
        self.api.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.api.ReadProcessMemory.restype = w.BOOL
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.api.CloseHandle.restype = w.BOOL
        self.handle = self.api.OpenProcess(0x1000 | 0x0010, False, pid)  # QUERY_LIMITED_INFORMATION | VM_READ
        if not self.handle:
            raise OSError(ctypes.get_last_error(), 'OpenProcess (read-only) failed')
        try:
            times = [w.FILETIME() for _ in range(4)]
            if not self.api.GetProcessTimes(self.handle, *(ctypes.byref(t) for t in times)):
                raise OSError(ctypes.get_last_error(), 'GetProcessTimes failed')
            start = times[0].dwLowDateTime | times[0].dwHighDateTime << 32
            if start != process_start:
                raise ValueError('process creation time mismatch (PID reused or wrong log)')
        except BaseException:
            self.close()
            raise

    def read(self, address):
        if not 0 < address < (1 << 47) - BUFFER_SIZE:
            raise ValueError('invalid trace buffer address')
        output = ctypes.create_string_buffer(BUFFER_SIZE)
        copied = ctypes.c_size_t()
        if not self.api.ReadProcessMemory(self.handle, address, output, BUFFER_SIZE, ctypes.byref(copied)) or copied.value != BUFFER_SIZE:
            raise OSError(ctypes.get_last_error(), 'trace read failed or was short')
        return output.raw

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, type=Path)
    args = parser.parse_args(argv)
    reader = None
    try:
        pid, start, address = identity_from_log(args.log)
        reader = WindowsReader(pid, start)
        report = decode(reader.read(address), reader.read(address), pid, start)
        json.dump(report, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 0
    except (OSError, ValueError) as exc:
        print(f'thread trace refused: {exc}', file=sys.stderr)
        return 1
    finally:
        if reader:
            reader.close()


if __name__ == '__main__':
    raise SystemExit(main())
