#!/usr/bin/env python3
"""Locate reviewed Death Stranding build 13300582 candidates without executing them."""
import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

PINNED_SHA256 = "9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b"
MAX_PATTERN_BYTES = 32
IMAGE_SCN_MEM_EXECUTE = 0x20000000


class SignatureError(ValueError):
    pass


@dataclass(frozen=True)
class RipRef:
    name: str
    instruction: int
    length: int


@dataclass(frozen=True)
class Candidate:
    name: str
    pattern: str
    refs: tuple


CANDIDATES = (
    Candidate(
        "DSPlayerEntity_sExportedGetLocalDSPlayerEntity",
        "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 ?? 33 C0 48 83 C4 28 C3 48 8D 15 ?? ?? ?? ??",
        (RipRef("player_manager_global", 4, 7), RipRef("dsplayer_type_metadata", 23, 7)),
    ),
    Candidate(
        "Player_sExportedGetLocalPlayer_impl",
        "85 C9 48 8B 0D ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 44 8B 0D ?? ?? ?? ??",
        (RipRef("player_manager_global", 2, 7), RipRef("player_selection_state", 24, 7)),
    ),
    Candidate(
        "WorldNode_orientation_matrix_body",
        "C5 FA 10 41 4C C5 FA 11 4C 24 14 C5 FA 10 49 40 C5 FA 11 44 24 18 C5 FA 10 05 ?? ?? ?? ?? C5 FA",
        (RipRef("matrix_constant", 22, 8),),
    ),
    Candidate(
        "MenuSaveDataDialog_sExportedIsSaveDataLoadingOrRequested",
        "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 81 30 03 00 00 48 63 89 28 03 00 00 4C 8D 04 C8 49 3B",
        (RipRef("save_dialog_manager_global", 0, 7),),
    ),
    Candidate(
        "DSPlayerEntity_sExportedHasBaggage_body",
        "4C 8B 05 ?? ?? ?? ?? 41 83 78 14 02 0F 8F ?? ?? ?? ?? 48 8B 88 D0 54 00 00 48 8D 54 24 30 33 ED",
        (RipRef("baggage_related_global", 0, 7),),
    ),
    Candidate(
        "DSSoundNodeGraphBindings_sGetCutsceneSoundPlaying",
        "40 53 48 83 EC 20 48 8B D9 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 ?? 88 0B 48 83 C4 20 5B C3",
        (RipRef("cutscene_sound_global", 9, 7),),
    ),
)


def parse_pattern(text):
    tokens = text.split()
    if not tokens:
        raise SignatureError("empty signature")
    if len(tokens) > MAX_PATTERN_BYTES:
        raise SignatureError(f"signature has {len(tokens)} bytes; maximum is {MAX_PATTERN_BYTES}")
    values = []
    mask = []
    for token in tokens:
        if token in ("?", "??"):
            values.append(0)
            mask.append(False)
            continue
        if len(token) != 2:
            raise SignatureError(f"invalid signature token {token!r}")
        try:
            values.append(int(token, 16))
        except ValueError as exc:
            raise SignatureError(f"invalid signature token {token!r}") from exc
        mask.append(True)
    if not any(mask):
        raise SignatureError("all-wildcard signature")
    return bytes(values), bytes(mask)


def find_matches(data, parsed):
    pattern, mask = parsed
    if len(pattern) > len(data):
        return []
    concrete = [i for i, enabled in enumerate(mask) if enabled]
    first = pattern[concrete[0]]
    matches = []
    for offset in range(len(data) - len(pattern) + 1):
        if data[offset + concrete[0]] != first:
            continue
        if all(data[offset + i] == pattern[i] for i in concrete[1:]):
            matches.append(offset)
    return matches


def validate_rip_ref(candidate, ref, parsed):
    _, mask = parsed
    if ref.instruction < 0 or ref.length < 6 or ref.instruction + ref.length > len(mask):
        raise SignatureError(f"{candidate.name}: invalid RIP reference bounds for {ref.name}")
    displacement = ref.instruction + ref.length - 4
    if any(mask[displacement:ref.instruction + ref.length]):
        raise SignatureError(f"{candidate.name}: RIP displacement must be fully wildcarded for {ref.name}")


def resolve_rip(data, match, ref, base_rva=0):
    end = match + ref.instruction + ref.length
    if ref.instruction < 0 or ref.length < 6 or end > len(data):
        raise SignatureError(f"RIP reference {ref.name} is outside the matched buffer")
    modrm = data[end - 5]
    if modrm & 0xC7 != 0x05:
        raise SignatureError(f"RIP reference {ref.name} does not have ModRM mod=00 r/m=101")
    displacement = struct.unpack_from("<i", data, end - 4)[0]
    target = base_rva + end + displacement
    if target < 0:
        raise SignatureError(f"RIP reference {ref.name} resolves before the image")
    return target


def scan_buffer(data, candidate, base_rva=0):
    parsed = parse_pattern(candidate.pattern)
    for ref in candidate.refs:
        validate_rip_ref(candidate, ref, parsed)
    matches = find_matches(data, parsed)
    if len(matches) != 1:
        raise SignatureError(f"{candidate.name}: expected exactly one match, found {len(matches)}")
    targets = {ref.name: resolve_rip(data, matches[0], ref, base_rva) for ref in candidate.refs}
    return matches[0], targets


class PEImage:
    def __init__(self, data):
        self.data = data
        try:
            if data[:2] != b"MZ":
                raise SignatureError("not an MZ image")
            pe = self._u32(0x3C)
            if data[pe:pe + 4] != b"PE\0\0":
                raise SignatureError("missing PE signature")
            coff = pe + 4
            section_count = self._u16(coff + 2)
            optional_size = self._u16(coff + 16)
            optional = coff + 20
            if self._u16(optional) != 0x20B or optional_size < 112:
                raise SignatureError("expected a PE32+ optional header")
            self.image_base = self._u64(optional + 24)
            self.sections = []
            table = optional + optional_size
            for index in range(section_count):
                offset = table + index * 40
                name = self.data[offset:offset + 8].rstrip(b"\0").decode("ascii")
                virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", self.data, offset + 8)
                characteristics = self._u32(offset + 36)
                if raw_size and (raw > len(data) or raw_size > len(data) - raw):
                    raise SignatureError(f"section {name} lies outside the file")
                self.sections.append(dict(name=name, rva=rva, virtual_size=virtual_size,
                                          raw=raw, raw_size=raw_size,
                                          characteristics=characteristics))
        except (IndexError, struct.error, UnicodeDecodeError) as exc:
            raise SignatureError("truncated or malformed PE image") from exc

    def _u16(self, offset):
        return struct.unpack_from("<H", self.data, offset)[0]

    def _u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def _u64(self, offset):
        return struct.unpack_from("<Q", self.data, offset)[0]

    def section_for_rva(self, rva):
        for section in self.sections:
            size = max(section["virtual_size"], section["raw_size"])
            if section["rva"] <= rva < section["rva"] + size:
                return section
        return None

    def scan(self, candidate):
        parse_pattern(candidate.pattern)
        matches = []
        for section in self.sections:
            if not section["characteristics"] & IMAGE_SCN_MEM_EXECUTE:
                continue
            start = section["raw"]
            blob = self.data[start:start + section["raw_size"]]
            for offset in find_matches(blob, parse_pattern(candidate.pattern)):
                matches.append((section, blob, offset))
        if len(matches) != 1:
            raise SignatureError(f"{candidate.name}: expected exactly one match, found {len(matches)}")
        section, blob, offset = matches[0]
        match_rva = section["rva"] + offset
        targets = {}
        for ref in candidate.refs:
            validate_rip_ref(candidate, ref, parse_pattern(candidate.pattern))
            target = resolve_rip(blob, offset, ref, section["rva"])
            target_section = self.section_for_rva(target)
            if target_section is None:
                raise SignatureError(f"{candidate.name}: {ref.name} target 0x{target:x} is outside all sections")
            if target_section["characteristics"] & IMAGE_SCN_MEM_EXECUTE:
                raise SignatureError(f"{candidate.name}: {ref.name} unexpectedly targets executable section")
            targets[ref.name] = dict(target_rva=f"0x{target:x}", section=target_section["name"])
        return dict(name=candidate.name, match_count=1, match_rva=f"0x{match_rva:x}",
                    references=targets)


def analyze(path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise SignatureError(f"executable SHA-256 mismatch: expected {PINNED_SHA256}, got {digest}")
    image = PEImage(data)
    return dict(build=13300582, sha256=digest, image_base=f"0x{image.image_base:x}",
                candidates=[image.scan(candidate) for candidate in CANDIDATES])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", help="path to the pinned executable image, opened read-only")
    args = parser.parse_args(argv)
    try:
        report = analyze(args.executable)
    except (OSError, SignatureError) as exc:
        print(f"signature analysis failed: {exc}", file=sys.stderr)
        return 1
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
