"""Verify the complete production adapter + core source identity (not compiler flags)."""
from pathlib import Path
import hashlib
import sys
root = Path(__file__).resolve().parent
names = ['../common/diag_log.h', 'bugmark.h', '../core/core.h', '../core/core.cpp', '../core/build_id.h',
         'controls.h', 'controls.cpp', 'voidout.h', 'voidout.cpp',
         'guard_win.h', 'enemies.h', 'enemies.cpp', 'enemy_poses.h', 'enemy_poses.cpp', 'mule_goal.h', 'mule_goal.cpp', 'orders.h', 'orders.cpp', 'story.h', 'story.cpp', 'rewards.h', 'rewards.cpp', '../launcher/saves.h', '../launcher/saves.cpp', 'orderload.h', 'orderload.cpp', 'environment.h', 'environment.cpp', 'join.h', 'join.cpp', 'game_thread.h', 'game_thread.cpp', 'puppet.h', 'puppet.cpp', 'double.h', 'double.cpp', 'pad.h', 'pad.cpp', 'ui.h', 'ui.cpp', 'cargo.h', 'cargo.cpp', 'carry_visual.h', 'carry_visual.cpp', 'marker.h', 'marker.cpp', 'help.h', 'help.cpp', 'notice.h', 'notice.cpp', 'strand_notices.h', 'strand_notices.cpp', '../world/json.h', 'online.h', 'online.cpp', 'vitals.h', 'vitals.cpp', 'structures.h', 'structures.cpp', 'vehicle.h', 'vehicle.cpp', 'discovery_options.h', 'discovery.h', 'discovery.cpp', 'discovery_signatures.h', 'timing.h', 'mod.h', 'mod.cpp', 'config.h', 'config.cpp', 'providers.h', 'providers.cpp', 'pose.h', 'pose.cpp', 'signature.h', 'signals.h', 'signals.cpp', '../core/relay.h', '../core/broadcast.h', '../core/broadcast_client.h', '../core/broadcast_client.cpp', 'others.h', 'others.cpp', 'crowd.h', 'crowd.cpp', 'fights.h', 'fights.cpp', 'route.h', 'route.cpp', 'testmode.h', 'testmode.cpp']
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
