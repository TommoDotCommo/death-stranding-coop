#!/usr/bin/env python3
"""Inventory loaded humanoid resource candidates with query/read handles only.

Native Windows Python; bounded partial heap scan, never calls game functions or
writes process memory. Output belongs outside the repository. A candidate is not
proof of object lifetime, spawn safety, or permission to remove the quarantine.
"""
import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import time
from find_actor_types import pinned_catalog
from probe_puppet import read, private_path
from read_thread_trace import WindowsReader, identity_from_log, decode


def readable_private(state, protect, kind):
    return state == 0x1000 and kind == 0x20000 and not protect & 0x100 and protect & 0xff in (2, 4, 8)


def scan(regions, read_bytes, targets, max_bytes, seconds, clock=time.monotonic):
    if not targets or not 1 <= max_bytes <= 4096 * 1024**2 or not math.isfinite(seconds) or not 0 < seconds <= 120:
        raise ValueError('invalid scan limits/targets')
    pattern = re.compile(b'|'.join(re.escape(struct.pack('<Q', v)) for v in targets))
    deadline = clock() + seconds
    attempted = scanned = failures = unstable = 0
    hits = []
    cursor = 0
    complete = True
    iterator = iter(regions)
    reason = 'address_space_exhausted'
    while True:
        if attempted >= max_bytes or clock() >= deadline or len(hits) >= 128:
            complete = False
            reason = 'budget_or_candidate_cap'
            break
        try:
            address, size = next(iterator)
        except StopIteration:
            break
        except TimeoutError:
            complete = False
            reason = 'enumeration_cap'
            break
        if address <= 0 or address % 8 or size <= 0 or address + size >= 1 << 47:
            raise ValueError('invalid memory region')
        offset = 0
        while offset < size:
            cursor = address + offset
            if attempted >= max_bytes or clock() >= deadline or len(hits) >= 128:
                complete = False
                reason = 'budget_or_candidate_cap'
                break
            length = min(1024**2, size - offset, max_bytes - attempted)
            attempted += length
            try:
                blob = read_bytes(cursor, length)
                if len(blob) != length:
                    raise OSError('short read')
            except OSError:
                failures += 1
                # Preserve aligned coverage on inaccessible regions; no byte search.
                offset += length
                continue
            scanned += length
            for match in pattern.finditer(blob):
                candidate = cursor + match.start()
                if candidate % 8:
                    continue
                vt = struct.unpack('<Q', match.group())[0]
                try:
                    first = read_bytes(candidate, 32)
                    second = read_bytes(candidate, 32)
                except OSError:
                    unstable += 1
                    continue
                if len(first) != 32 or first != second or struct.unpack_from('<Q', first)[0] != vt:
                    unstable += 1
                    continue
                hits.append(dict(address=hex(candidate), resource_type=targets[vt]['name'],
                    vtable=hex(vt), static_entity_type=targets[vt].get('static_entity_type', 'unknown'),
                    player_resource=targets[vt]['player_resource'], spawn_approved=False))
                if len(hits) >= 128:
                    complete = False
                    reason = 'candidate_cap'
                    cursor = candidate + 8
                    break
            if not complete:
                break
            offset += length
            cursor = address + offset
        if not complete:
            break
    return dict(candidates=hits, attempted_bytes=attempted, scanned_bytes=scanned,
                failed_chunks=failures, unstable_candidates=unstable, address_space_exhausted=complete,
                next_address=hex(cursor), stop_reason=reason, spawn_approved=False,
                limitations='partial concurrent scan; stable headers do not prove lifetime or safe creation')


class ResourceReader(WindowsReader):
    def __init__(self, pid, start):
        super().__init__(pid, start)
        from ctypes import wintypes as w
        # VirtualQueryEx needs QUERY_INFORMATION; retain VM_READ only for memory.
        query = self.api.OpenProcess(0x0400 | 0x0010, False, pid)
        if not query:
            self.close()
            raise OSError(c.get_last_error(), 'OpenProcess query/read failed')
        try:
            times = [w.FILETIME() for _ in range(4)]
            if not self.api.GetProcessTimes(query, *(c.byref(t) for t in times)):
                raise OSError('query handle process times unavailable')
            if times[0].dwLowDateTime | times[0].dwHighDateTime << 32 != start:
                raise ValueError('query handle process creation time mismatch')
        except BaseException:
            self.api.CloseHandle(query)
            self.close()
            raise
        self.api.CloseHandle(self.handle)
        self.handle = query
        class MBI(c.Structure):
            _fields_ = [('base', c.c_void_p), ('allocation', c.c_void_p), ('allocation_protect', w.DWORD),
                        ('partition', w.WORD), ('size', c.c_size_t), ('state', w.DWORD),
                        ('protect', w.DWORD), ('kind', w.DWORD)]
        self.MBI = MBI
        self.api.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MBI), c.c_size_t]
        self.api.VirtualQueryEx.restype = c.c_size_t
        self.api.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
        self.api.QueryFullProcessImageNameW.restype = w.BOOL

    def image_path(self):
        from ctypes import wintypes as w
        buffer = c.create_unicode_buffer(32768)
        size = w.DWORD(len(buffer))
        if not self.api.QueryFullProcessImageNameW(self.handle, 0, buffer, c.byref(size)):
            raise OSError('process image path unavailable')
        return Path(buffer.value)

    def regions(self, begin, deadline):
        address = begin
        count = 0
        while address < (1 << 47) - 65536:
            if time.monotonic() >= deadline or count >= 1000000:
                raise TimeoutError('memory region enumeration cap reached; partial output required')
            info = self.MBI()
            if self.api.VirtualQueryEx(self.handle, address, c.byref(info), c.sizeof(info)) != c.sizeof(info):
                # Windows reports ERROR_INVALID_PARAMETER above its maximum VA.
                if c.get_last_error() == 87:
                    return
                raise OSError(c.get_last_error(), 'VirtualQueryEx failed')
            end = (info.base or 0) + info.size
            if end <= address:
                raise ValueError('memory region did not advance')
            count += 1
            if readable_private(info.state, info.protect, info.kind):
                yield max(address, info.base or 0), end - max(address, info.base or 0)
            address = end


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--max-mib', type=int, default=1024)
    p.add_argument('--seconds', type=float, default=20)
    p.add_argument('--start-address', type=lambda s: int(s, 0), default=65536)
    a = p.parse_args(argv)
    if os.name != 'nt':
        p.error('native Windows Python required')
    if not 1 <= a.max_mib <= 4096 or not math.isfinite(a.seconds) or not 1 <= a.seconds <= 120 or not 65536 <= a.start_address < (1 << 47) or a.start_address % 8:
        p.error('invalid scan limits')
    reader = None
    try:
        audit = a.log.absolute()
        private_path(audit)
        if audit.parent.name.casefold() != 'redirect-data':
            raise ValueError('disposable redirect-data log required')
        pid, start, trace = identity_from_log(audit)
        if '\nNETWORK\tenforce\t' not in '\n' + audit.read_text(encoding='utf-16-le'):
            raise ValueError('enforced-network audit record required')
        reader = ResourceReader(pid, start)
        exe = reader.image_path()
        if exe.resolve() != (audit.parent.parent / 'ds.exe').resolve():
            raise ValueError('process image does not match disposable copy')
        types = pinned_catalog(exe)
        snapshot = decode(reader.read(trace), reader.read(trace), pid, start)
        base = int(snapshot['host_base'], 16)
        targets = {base + int(t['vtable_candidate_rva'], 16): t for t in types['types']}
        # Check runtime relocation and getter identity before searching heaps.
        for vt, row in targets.items():
            getter = base + int(row['getter_rva'], 16)
            if read(reader, vt, 8) != struct.pack('<Q', getter):
                raise ValueError('loaded resource table differs from pinned image')
            code = read(reader, getter, 8)
            if code[:3] != b'\x48\x8d\x05' or code[-1:] != b'\xc3' or getter + 7 + struct.unpack_from('<i', code, 3)[0] != base + int(row['metadata_rva'], 16):
                raise ValueError('loaded resource type getter differs from pinned image')
        report = scan(reader.regions(a.start_address, time.monotonic() + a.seconds),
                      lambda address, size: read(reader, address, size), targets, a.max_mib * 1024**2, a.seconds)
        report.update(pid=pid, process_start=hex(start), host_base=hex(base), sha256=types['sha256'])
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, TimeoutError) as error:
        print(json.dumps(dict(ok=False, error=str(error), spawn_approved=False)))
        return 1
    finally:
        if reader:
            reader.close()


if __name__ == '__main__':
    raise SystemExit(main())
