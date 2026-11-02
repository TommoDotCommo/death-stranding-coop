#!/usr/bin/env python3
"""Read-only, pinned-file catalog of humanoid resource type/vtable candidates.

A class name or inheritance match never approves a resource for engine calls.
Only the reviewed lea-rax/ret getter form and file-backed class metadata are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from find_message_calls import Inventory, PINNED_SHA256, SignatureError


def catalog(data):
    image = Inventory(data)
    base = struct.unpack_from('<Q', data, image.optional + 24)[0]
    def pointer(rva):
        return struct.unpack('<Q', image.read(rva, 8))[0] - base
    def metadata(rva):
        section = image.section_for_rva(rva)
        if not section or image.executable(rva) or rva % 8:
            raise SignatureError('class metadata is not aligned image data')
        if image.read(rva, 6) != b'\xff\xff\xff\xff\x04\x00':
            raise SignatureError('unsupported class metadata header')
        name = image.cstring(pointer(rva + 0x40))
        if not re.fullmatch('[A-Za-z_][A-Za-z0-9_]{0,127}', name):
            raise SignatureError('unsupported class name')
        count = image.read(rva + 6, 1)[0]
        if count > 8:
            raise SignatureError('class base count exceeds cap')
        parents = []
        if count:
            table = pointer(rva + 0x60)
            for index in range(count):
                parent = pointer(table + 16 * index)
                # Base adjustments describe inheritance only; no object address
                # is derived from them by this catalog.
                parents.append(parent)
        return name, parents
    found = {}
    getters = {}
    for section in image.sections:
        if not image.executable(section['rva']):
            continue
        blob = image.read(section['rva'], min(section['raw_size'], section['virtual_size']))
        for match in re.finditer(rb'\x48\x8d\x05[\s\S]{4}\xc3', blob):
            site = section['rva'] + match.start()
            target = site + 7 + struct.unpack_from('<i', match.group(), 3)[0]
            try:
                metadata(target)
            except SignatureError:
                continue
            getters[site] = target
    # Follow metadata links to bases without getter functions too.
    def visit(rva, stack=()):
        if rva in stack or len(stack) >= 32:
            raise SignatureError('cyclic or overdeep class ancestry')
        if rva in found:
            return found[rva]
        name, parents = metadata(rva)
        names = [name]
        for parent in parents:
            names.extend(visit(parent, (*stack, rva)))
        found[rva] = list(dict.fromkeys(names))
        return found[rva]
    selected = {}
    for getter, rva in getters.items():
        try:
            ancestry = visit(rva)
        except SignatureError:
            continue
        if 'HumanoidResource' in ancestry:
            selected[getter] = (rva, ancestry)
    tables = {getter: [] for getter in selected}
    # Pointer slots are candidates; the runtime reader additionally checks loaded
    # getter identity and repeated exact reads. No table is treated as a live object.
    for section in image.sections:
        if image.executable(section['rva']):
            continue
        blob = image.read(section['rva'], section['raw_size'])
        for offset in range((-section['rva']) % 8, len(blob) - 7, 8):
            value = struct.unpack_from('<Q', blob, offset)[0] - base
            if value in selected:
                tables[value].append(section['rva'] + offset)
    rows = []
    for getter, (rva, ancestry) in sorted(selected.items()):
        for table in tables[getter]:
            try:
                factory = pointer(table + 0x80)
                if not image.executable(factory):
                    continue
            except SignatureError:
                continue
            entity_type = 'unknown'
            # Shared resource factories select their allocated entity via +68.
            # Report only the two reviewed eight-byte getter forms.
            try:
                type_getter = pointer(table + 0x68)
                if image.executable(type_getter):
                    code = image.read(type_getter, 8)
                    if code[-1:] == b'\xc3' and code[:3] in (b'\x48\x8d\x05', b'\x48\x8b\x05'):
                        target = type_getter + 7 + struct.unpack_from('<i', code, 3)[0]
                        entity_meta = pointer(target) if code[1] == 0x8b else target
                        entity_type = metadata(entity_meta)[0]
            except SignatureError:
                pass
            rows.append(dict(name=ancestry[0], static_entity_type=entity_type, metadata_rva=hex(rva), ancestry=ancestry,
                getter_rva=hex(getter), vtable_candidate_rva=hex(table), factory_candidate_rva=hex(factory),
                player_resource='DSPlayerEntityResource' in ancestry, spawn_approved=False))
    return dict(image_base=hex(base), types=rows, spawn_approved=False,
                limitations='static type/table candidates; no resource instance or safe spawn contract established')


def pinned_catalog(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != PINNED_SHA256:
        raise SignatureError('executable hash mismatch')
    result = catalog(data)
    result['sha256'] = PINNED_SHA256
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(pinned_catalog(args.exe), indent=2))
        return 0
    except (OSError, ValueError) as error:
        print('actor catalog refused: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
