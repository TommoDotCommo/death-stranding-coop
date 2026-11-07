#!/usr/bin/env python3
"""Game-free native Windows fixture: absent launcher config must not close a game.

Run with native Windows Python and an existing disposable --work-dir. Clears COOP_*,
uses a fake LOCALAPPDATA, and never opens real saves or starts ds.exe.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--proxy', type=Path, required=True)
    p.add_argument('--mutant', type=Path, required=True)
    a = p.parse_args()
    if os.name != 'nt':
        raise SystemExit('Run with native Windows Python; no game is needed.')
    if not a.work_dir.is_dir():
        raise SystemExit('Existing disposable --work-dir required.')
    for name, proxy, should_pass in [('release', a.proxy, True), ('missing-config-closes', a.mutant, False)]:
        with tempfile.TemporaryDirectory(prefix='ds-pass-through-', dir=a.work_dir) as d:
            root = Path(d)
            shutil.copyfile(a.fixture, root/'fixture.exe')
            shutil.copyfile(proxy, root/'avrt.dll')
            local = root/'fake-local'
            local.mkdir()
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith('COOP_')}
            env['LOCALAPPDATA'] = str(local)
            result = subprocess.run([str(root/'fixture.exe')], env=env, cwd=root,
                                    capture_output=True, text=True, timeout=30)
            passed = False
            if result.returncode == 0:
                report = json.loads(result.stdout.strip().splitlines()[-1])
                passed = report['proxy_first'] and report['forwarded'] and not report['env_redirected'] and not list(local.iterdir())
            if bool(passed) != should_pass:
                raise SystemExit(f'{name}: incorrect pass-through: {result.returncode}, {result.stdout}, {result.stderr}')
            if not should_pass and result.returncode != 125:
                raise SystemExit(f'{name}: expected missing-config exit 125, got {result.returncode}')
            print(name, 'PASS' if passed else 'mutant killed (125)')


if __name__ == '__main__':
    main()
