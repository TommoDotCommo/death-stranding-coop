#!/usr/bin/env python3
"""Build the drop-in package folder: proxy + mod + coop-mod.json (J5). Usage:

    make_package.py --out DIR --version V --proxy redirect/build/avrt.dll --mod mod/build/mod.dll
"""
import argparse
import json
from pathlib import Path
import shutil

from coop_install import MANIFEST, sha256_file

GAME = dict(name='DEATH STRANDING DIRECTOR\'S CUT', steam_appid=1850570, exe=['ds.exe'],
            builds=[dict(name='Steam build 13300582',
                         sha256='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b')])


def build_package(out, version, proxy, mod, proxy_name='avrt.dll', builds=None):
    out = Path(out)
    out.mkdir(parents=True)
    layout = [('game', proxy_name, proxy), ('data', 'redirect-data/mod/mod.dll', mod)]
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
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--proxy', required=True)
    p.add_argument('--mod', required=True)
    a = p.parse_args()
    build_package(a.out, a.version, a.proxy, a.mod)
