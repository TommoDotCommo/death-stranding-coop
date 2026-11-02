#!/usr/bin/env python3
"""Read-only, hash-pinned inventory of message-pump import references.

Byte matches are candidates, not decoded instructions or proof of thread safety.
Use the reported PE exception-directory bounds for focused disassembly.
"""
import argparse
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from find_signatures import PEImage, PINNED_SHA256, SignatureError, IMAGE_SCN_MEM_EXECUTE

TARGETS = ('PeekMessageA', 'GetMessageA')


class Inventory(PEImage):
    def __init__(self, data):
        super().__init__(data)
        pe = self._u32(0x3c)
        optional = pe + 24
        if self._u16(pe + 4) != 0x8664:
            raise SignatureError('expected AMD64 image')
        self.optional = optional
        self.optional_size = self._u16(pe + 20)
        self.directory_count = self._u32(optional + 108)
        self.functions = self.function_ranges()
        self.starts = [f[0] for f in self.functions]

    def offset(self, rva, size=1):
        if rva < 0 or size < 1:
            raise SignatureError('invalid RVA read')
        matches = [s for s in self.sections if s['rva'] <= rva and
                   rva + size <= s['rva'] + s['raw_size']]
        if len(matches) != 1:
            raise SignatureError(f'RVA 0x{rva:x} is not uniquely file-backed ({size} bytes)')
        return matches[0]['raw'] + rva - matches[0]['rva']

    def read(self, rva, size):
        offset = self.offset(rva, size)
        return self.data[offset:offset + size]

    def directory(self, index):
        if index >= self.directory_count:
            return 0, 0
        if 112 + 8 * (index + 1) > self.optional_size:
            raise SignatureError('data directory outside optional header')
        return struct.unpack_from('<II', self.data, self.optional + 112 + index * 8)

    def cstring(self, rva):
        offset = self.offset(rva)
        section = self.section_for_rva(rva)
        end = self.data.find(b'\0', offset, min(offset + 512, section['raw'] + section['raw_size']))
        if end == -1:
            raise SignatureError('unterminated import string')
        try:
            return self.data[offset:end].decode('ascii')
        except UnicodeDecodeError as exc:
            raise SignatureError('non-ASCII import string') from exc

    def executable(self, rva):
        section = self.section_for_rva(rva)
        return section is not None and bool(section['characteristics'] & IMAGE_SCN_MEM_EXECUTE)

    def function_ranges(self):
        rva, size = self.directory(3)
        if not rva or not size or size % 12:
            raise SignatureError('missing or malformed AMD64 exception directory')
        entries = []
        for begin, end, unwind in struct.iter_unpack('<III', self.read(rva, size)):
            if begin >= end or not self.executable(begin) or not self.executable(end - 1):
                raise SignatureError('invalid runtime function bounds')
            self.offset(unwind)
            if entries and begin < entries[-1][1]:
                raise SignatureError('unsorted or overlapping runtime functions')
            entries.append((begin, end))
        return entries

    def containing(self, rva, size=1):
        index = bisect_right(self.starts, rva) - 1
        if index >= 0:
            begin, end = self.functions[index]
            if begin <= rva and rva + size <= end:
                return {'begin_rva': hex(begin), 'end_rva': hex(end)}
        return None

    def imports(self):
        rva, size = self.directory(1)
        if not rva or size < 20:
            raise SignatureError('missing import directory')
        imports = []
        for at in range(0, size - 19, 20):
            original, stamp, chain, name, iat = struct.unpack('<IIIII', self.read(rva + at, 20))
            if not any((original, stamp, chain, name, iat)):
                return imports
            if not original or not iat:
                raise SignatureError('missing import lookup table or IAT')
            dll = self.cstring(name)
            # A missing terminator must not scan indefinitely into arbitrary sections.
            for index in range(65536):
                thunk, = struct.unpack('<Q', self.read(original + 8 * index, 8))
                self.offset(iat + 8 * index, 8)
                if not thunk:
                    break
                if thunk & (1 << 63):
                    continue
                symbol = self.cstring(thunk + 2)
                if symbol in TARGETS:
                    if dll.lower() != 'user32.dll':
                        raise SignatureError(f'unexpected DLL for {symbol}: {dll}')
                    imports.append({'name': symbol, 'dll': dll, 'iat_rva': iat + 8 * index})
            else:
                raise SignatureError('unterminated import lookup table')
        raise SignatureError('unterminated import descriptor table')

    def code_address_references(self, target):
        result = []
        for section in self.sections:
            if not section['characteristics'] & IMAGE_SCN_MEM_EXECUTE:
                continue
            blob = self.read(section['rva'], min(section['raw_size'], section['virtual_size']))
            for match in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', blob):
                offset = match.start()
                if offset + 7 > len(blob):
                    continue
                site = section['rva'] + offset
                if site + 7 + struct.unpack_from('<i', blob, offset + 3)[0] == target:
                    result.append({'kind': 'lea_code_address', 'site_rva': hex(site),
                        'function': self.containing(site, 7), 'decoded': False})
        return result

    def references(self, imports):
        targets = {entry['iat_rva']: entry['name'] for entry in imports}
        refs = {name: [] for name in TARGETS}
        thunks = {}
        code_sections = [s for s in self.sections if s['characteristics'] & IMAGE_SCN_MEM_EXECUTE]
        for section in code_sections:
            blob = self.read(section['rva'], min(section['raw_size'], section['virtual_size']))
            # AMD64 call/jmp qword ptr [rip+disp32]. This intentionally does not
            # present a byte-pattern match as a disassembler result.
            for match in re.finditer(rb'\xff[\x15\x25]', blob):
                offset = match.start()
                if offset + 6 > len(blob):
                    continue
                site = section['rva'] + offset
                displacement, = struct.unpack_from('<i', blob, offset + 2)
                target = site + 6 + displacement
                if target not in targets:
                    continue
                kind = 'indirect_call' if blob[offset + 1] == 0x15 else 'indirect_jump'
                refs[targets[target]].append({'kind': kind, 'site_rva': hex(site),
                    'function': self.containing(site, 6), 'decoded': False})
                if kind == 'indirect_jump':
                    thunks[site] = targets[target]
        # Include rel32 callers of import jump stubs; never mistake a stub for
        # the game function which actually invokes the imported API.
        if thunks:
            for section in code_sections:
                blob = self.read(section['rva'], min(section['raw_size'], section['virtual_size']))
                for match in re.finditer(rb'[\xe8\xe9]', blob):
                    offset = match.start()
                    if offset + 5 > len(blob):
                        continue
                    site = section['rva'] + offset
                    target = site + 5 + struct.unpack_from('<i', blob, offset + 1)[0]
                    if target in thunks:
                        refs[thunks[target]].append({'kind': 'call_thunk' if blob[offset] == 0xe8 else 'jump_thunk',
                            'site_rva': hex(site), 'thunk_rva': hex(target),
                            'function': self.containing(site, 5), 'decoded': False})
        return refs


def analyze(path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise SignatureError(f'executable SHA-256 mismatch: expected {PINNED_SHA256}, got {digest}')
    image = Inventory(data)
    imports = image.imports()
    for name in TARGETS:
        if sum(entry['name'] == name for entry in imports) != 1:
            raise SignatureError(f'expected exactly one USER32 import of {name}')
    refs = image.references(imports)
    functions = sorted({int(ref['function']['begin_rva'], 16) for group in refs.values() for ref in group if ref['function']})
    return {'build': 13300582, 'sha256': digest, 'image_base': hex(image.image_base),
        'method': 'byte candidates, not decoded instructions; ordinary imports only; register-indirect calls not inventoried',
        'safe_game_thread': 'unknown',
        'pump_address_references': {hex(target): image.code_address_references(target) for target in functions},
        'imports': [dict(entry, iat_rva=hex(entry['iat_rva']), candidates=refs[entry['name']]) for entry in imports]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', help='pinned ds.exe, opened read-only')
    args = parser.parse_args(argv)
    try:
        report = analyze(args.executable)
    except (OSError, ValueError, struct.error) as exc:
        print(f'message-call analysis failed: {exc}', file=sys.stderr)
        return 1
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
