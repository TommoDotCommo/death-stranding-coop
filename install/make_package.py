#!/usr/bin/env python3
"""Build the drop-in package folder: proxy + mod + coop-mod.json (J5). Usage:

    make_package.py --out DIR --version V --proxy redirect/build/avrt.dll --mod mod/build/mod.dll
                    [--launcher launcher/build/deathstranding-coop.exe] [--world world/build/coop-world.exe]
"""
import argparse
import json
from pathlib import Path
import shutil

from coop_install import MANIFEST, sha256_file

GAME = dict(name='DEATH STRANDING DIRECTOR\'S CUT', steam_appid=1850570, exe=['ds.exe'],
            builds=[dict(name='Steam build 13300582',
                         sha256='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b')])


HOW_TO = '''Death Stranding co-op {version} (experimental, two players)

You need: DEATH STRANDING DIRECTOR'S CUT on Steam (build 13300582), installed, and Steam running.

1. Extract this whole folder anywhere (keep the files together).
2. Start deathstranding-coop.exe. It offers to install the mod into your game: choose Yes.
   It adds one file (avrt.dll) to the game folder. Nothing of the game is replaced.
   Co-op saves are separate from your ordinary saves (%LOCALAPPDATA%\\DeathStrandingCoop).
3. First time: Saves, "Copy my Steam saves into co-op", so co-op continues your story instead of
   starting a new one. Only the host needs this: a guest can start from the host's save.
4. Host: Play, Host a game, Open lobby, Copy invite, send it to your friend.
   Guest: Play, Join a friend, paste the invite, Join, then Ready.
   Leave "Start from the host's save" ticked to play in the same world and episode.
   Host: Start game. In the game both choose Continue.
5. Windows Firewall asks once whether the game may use the network: choose Allow.
   Not on the same network? Both install Tailscale (or another VPN) and use the same invite.

Keys in the game: J rides along on your partner's truck or bike (and gets off), B pings the map.
Something wrong? About, "Save diagnostics for a bug report", and send that file.
Remove: About, "Remove co-op from the game". Starting the game from Steam as usual plays without co-op.
'''


def build_package(out, version, proxy, mod, proxy_name='avrt.dll', builds=None, launcher=None, world=None):
    out = Path(out)
    out.mkdir(parents=True)
    layout = [('game', proxy_name, proxy), ('data', 'redirect-data/mod/mod.dll', mod)]
    if launcher is not None:
        layout.append(('game', 'deathstranding-coop.exe', launcher))
    if world is not None:   # the shared-world server the launcher starts for a host (world/README.md)
        layout.append(('game', 'coop-world.exe', world))
    files = []
    for root, rel, source in layout:
        dest = out/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        files.append(dict(root=root, path=rel, sha256=sha256_file(dest), size=dest.stat().st_size))
    game = dict(GAME)
    if builds is not None:
        game['builds'] = builds
    manifest = dict(schema=1, id='death-stranding-coop', name='Death Stranding co-op', version=version, game=game,
                    proxy=dict(file=proxy_name, system_dll=proxy_name, chain_file=proxy_name[:-4]+'.chain.dll'),
                    proton=dict(dll_overrides=[proxy_name[:-4]+'=n,b']), files=files,
                    user_data='%LOCALAPPDATA%\\DeathStrandingCoop')
    (out/MANIFEST).write_text(json.dumps(manifest, indent=1), encoding='utf-8')
    if launcher is not None:
        (out/'HOW-TO-PLAY.txt').write_text(HOW_TO.format(version=version), encoding='utf-8', newline='\r\n')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--proxy', required=True)
    p.add_argument('--mod', required=True)
    p.add_argument('--launcher', help='built deathstranding-coop.exe to include beside ds.exe')
    p.add_argument('--world', help='built coop-world.exe to include beside the launcher')
    a = p.parse_args()
    build_package(a.out, a.version, a.proxy, a.mod, launcher=a.launcher, world=a.world)
