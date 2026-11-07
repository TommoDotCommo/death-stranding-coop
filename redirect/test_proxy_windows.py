#!/usr/bin/env python3
"""Native Windows, game-free: the avrt.dll proxy in a NEW synthetic fixture folder.

A fixture exe that imports avrt.dll statically (as ds.exe does) runs from a temporary folder holding the proxy. Checks:
the proxy loaded first (the module bound to the exe's import is the folder's avrt.dll, not System32's), calls forward
to the real System32 DLL (same results as calling it directly, including a 5-argument call), the redirect initialised
(known-folder environment and a file under the "real" folder land in the instance) and the first forwarded call started
the mod bootstrap. Negative controls: no proxy in the folder, and a mutant proxy with broken forwarding, must fail.
Never launches ds.exe or touches a game or save root. --work-dir must be an existing disposable drive folder.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def winpath(path):
    """Windows spelling of a path. Under WSL the child is a Windows process, so paths in its environment must be."""
    if os.name == 'nt':
        return str(path)
    return subprocess.run(['wslpath', '-w', str(path)], capture_output=True, text=True, check=True).stdout.strip()


def run_case(work, fixture, proxy):
    root = work/('avrt-proxy-'+uuid.uuid4().hex)
    instance = root/'instance'
    data = instance/'redirect-data'
    (data/'mod').mkdir(parents=True)
    for name in ('local', 'roaming', 'documents', 'savedgames', 'profile'):
        (data/name).mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith('COOP_')}
    env.update(COOP_REDIRECT_INSTANCE=winpath(data), COOP_MODULE_INSTANCE=winpath(instance), COOP_MODULE_POLICY='allowlist',
               COOP_NETWORK_POLICY='enforce')
    reals = {}
    for key in ('LOCAL', 'ROAMING', 'DOCUMENTS', 'SAVEDGAMES'):
        reals[key] = root/('real-'+key.lower())
        reals[key].mkdir()
        env['COOP_REAL_'+key] = winpath(reals[key])
    (root/'fixture-steam').mkdir()
    env['COOP_REDIRECT_STEAM'] = winpath(root/'fixture-steam')
    if os.name != 'nt':  # WSL passes only the variables listed in WSLENV to Windows processes
        env['WSLENV'] = ':'.join(k for k in env if k.startswith('COOP_'))
    shutil.copyfile(fixture, instance/'fixture.exe')
    if proxy:
        shutil.copyfile(proxy, instance/'avrt.dll')
    r = subprocess.run([str(instance/'fixture.exe')], cwd=instance, env=env, capture_output=True, text=True, timeout=60)
    out = dict(fixture=str(root), exit=r.returncode, stderr=r.stderr[-300:])
    if r.returncode == 0:
        out['report'] = json.loads(r.stdout.strip().splitlines()[-1])
        logs = list(data.glob('redirect-*.log'))
        out['mod_bootstrap'] = bool(logs) and 'MOD_ABSENT' in logs[0].read_text(encoding='utf-16-le', errors='replace')
        out['marker_in_instance'] = (data/'local'/'proxy-fixture-marker.txt').exists()
        out['marker_in_real'] = (reals['LOCAL']/'proxy-fixture-marker.txt').exists()
    return out


def passes(case):
    rep = case.get('report')
    return bool(case['exit'] == 0 and rep and rep['proxy_first'] and rep['real_distinct'] and rep['forwarded']
                and rep['env_redirected'] and rep['marker_error'] == 0 and case['mod_bootstrap']
                and case['marker_in_instance'] and not case['marker_in_real'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--proxy', type=Path, required=True)
    p.add_argument('--mutant', type=Path, required=True, help='proxy with broken forwarding; must fail')
    a = p.parse_args()
    if not a.work_dir.is_dir():
        raise SystemExit('existing disposable --work-dir required')
    good = run_case(a.work_dir, a.fixture, a.proxy)
    no_proxy = run_case(a.work_dir, a.fixture, None)
    broken = run_case(a.work_dir, a.fixture, a.mutant)
    ok = passes(good) and not passes(no_proxy) and not passes(broken)
    if ok:  # keep the fixture folders of a failing run for inspection
        for case in (good, no_proxy, broken):
            shutil.rmtree(case['fixture'], ignore_errors=True)
    print(json.dumps(dict(ok=ok, good=good, no_proxy_must_fail=no_proxy, mutant_must_fail=broken), indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
