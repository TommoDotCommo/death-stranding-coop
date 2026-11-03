#!/usr/bin/env python3
"""Native Windows, game-free: the virtual pad through the real proxy and mod in a NEW synthetic fixture tree.

Never launches ds.exe or touches a game or save root. --work-dir must be an existing disposable drive folder.
Also checks that the proxy refuses a mismatch between COOP_VIRTUAL_PAD and the mod's virtual_pad.
"""
import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time
import uuid

SYSTEM = Path(os.environ.get('SystemRoot', r'C:\Windows'))/'System32'


def run_case(args, pad_env, pad_config):
    root = args.work_dir/('pad-probe-'+uuid.uuid4().hex)
    instance = root/'instance'
    data = instance/'redirect-data'
    (data/'mod').mkdir(parents=True)
    for name in ('local', 'roaming', 'documents', 'savedgames', 'profile'):
        (data/name).mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith('COOP_')}
    env.update(COOP_REDIRECT_INSTANCE=str(data), COOP_MODULE_INSTANCE=str(instance), COOP_MODULE_POLICY='allowlist',
               COOP_NETWORK_POLICY='enforce', COOP_VIRTUAL_PAD=pad_env)
    for key in ('LOCAL', 'ROAMING', 'DOCUMENTS', 'SAVEDGAMES'):
        real = root/('real-'+key.lower())
        real.mkdir()
        env['COOP_REAL_'+key] = str(real)
    (root/'fixture-steam').mkdir()
    env['COOP_REDIRECT_STEAM'] = str(root/'fixture-steam')
    for source, target in ((args.proxy, instance/'d3dcompiler_47.dll'), (args.probe, instance/'probe.exe'),
                           (SYSTEM/'d3dcompiler_47.dll', instance/'d3dcompiler_47_original.dll'),
                           (args.mod, data/'mod'/'mod.dll')):
        with source.open('rb') as src, target.open('xb') as dst:
            shutil.copyfileobj(src, dst)
    (data/'mod'/'mod.json').write_text(json.dumps(dict(role='host', peer='127.0.0.1', slot=14, peer_slot=15, nickname='pad',
                                                         provider='synthetic', session=7, required=True, self_tick=True,
                                                         virtual_pad=pad_config)))
    child = subprocess.Popen([str(instance/'probe.exe'), str(instance/'d3dcompiler_47.dll')], cwd=instance, env=env,
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        lines = queue.Queue()
        threading.Thread(target=lambda: [lines.put(l) for l in iter(child.stdout.readline, '')] + [lines.put('')],
                         daemon=True).start()
        first = lines.get(timeout=20)
        if not first:
            _, err = child.communicate(timeout=5)
            return dict(fixture=str(root), exit=child.returncode, stderr=err[-400:])
        assert json.loads(first)['pid'] == child.pid, first
        logs = list((data/'mod').glob('mod-*.log'))
        assert len(logs) == 1, logs
        stamp = logs[0].stem[len('mod-'):]
        (data/'mod'/'pad.script').write_text(f'pad {stamp}\npress A 400\nstick 0 1 400\n')
        deadline = time.monotonic()+5
        while (data/'mod'/'pad.script').exists() and time.monotonic() < deadline:
            time.sleep(.05)
        assert not (data/'mod'/'pad.script').exists(), 'mod did not consume the pad script'
        child.stdin.write('go\n')
        child.stdin.flush()
        report = json.loads(lines.get(timeout=20))
        child.wait(timeout=10)
        log = logs[0].read_text(errors='replace')
        audit = next(data.glob(f'redirect-{child.pid}-*.log')).read_text(encoding='utf-16-le')
        return dict(fixture=str(root), exit=child.returncode, report=report, started='PAD script started steps=3' in log,
                    finished='PAD script finished' in log, audit_wrapped='XInputGetState wrapped' in audit)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--proxy', type=Path, required=True)
    p.add_argument('--probe', type=Path, required=True)
    p.add_argument('--mod', type=Path, required=True)
    a = p.parse_args()
    if not a.work_dir.is_dir():
        raise SystemExit('existing disposable --work-dir required')
    good = run_case(a, '1', True)
    ok = good.get('exit') == 0 and good.get('started') and good.get('audit_wrapped') and not good['report']['failures']
    refusals = [run_case(a, '1', False), run_case(a, '0', True), run_case(a, 'yes', True)]
    refused = all(r.get('exit') == 125 for r in refusals)
    print(json.dumps(dict(ok=bool(ok and refused), case=good, refusals=refusals), indent=1))
    return 0 if ok and refused else 1


if __name__ == '__main__':
    raise SystemExit(main())
