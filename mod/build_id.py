"""Verify the complete production adapter + core source identity (not compiler flags)."""
from pathlib import Path
import hashlib
import sys
root = Path(__file__).resolve().parent
names = ['../core/core.h', '../core/core.cpp', '../core/build_id.h',
         'environment.h', 'environment.cpp', 'join.h', 'join.cpp', 'game_thread.h', 'game_thread.cpp', 'puppet.h', 'puppet.cpp', 'double.h', 'double.cpp', 'pad.h', 'pad.cpp', 'cargo.h', 'cargo.cpp', 'carry_visual.h', 'carry_visual.cpp', 'marker.h', 'marker.cpp', 'notice.h', 'notice.cpp', 'structures.h', 'structures.cpp', 'vehicle.h', 'vehicle.cpp', 'discovery_options.h', 'discovery.h', 'discovery.cpp', 'discovery_signatures.h', 'timing.h', 'mod.h', 'mod.cpp', 'config.h', 'config.cpp', 'providers.h', 'providers.cpp', 'pose.h', 'pose.cpp', 'signature.h']
h = hashlib.sha256()
for name in names:
    b = (root/name).read_bytes()
    h.update(name.encode()+b'\0'+len(b).to_bytes(8, 'little')+b)
identity = h.digest()[:16]
s = '#pragma once\n#include <array>\nnamespace mod { constexpr std::array<unsigned char,16> WireBuild = {' + ','.join(str(x) for x in identity) + '}; }\n'
p = root/'identity.h'
if sys.argv[1:] == ['--write']:
    p.write_text(s)
elif not p.exists() or p.read_text() != s:
    sys.exit('adapter identity stale: review changes, then python3 build_id.py --write')
print('adapter_build=' + identity.hex())
