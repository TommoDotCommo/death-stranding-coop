#!/usr/bin/env python3
"""Native Windows, game-free integration probe in NEW synthetic fixture trees.

Retains all evidence. Never launches ds.exe, injects into another process or
opens a game/save root. --work-dir must be an existing disposable drive folder.
"""
import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import uuid

from read_thread_trace import WindowsReader, decode, identity_from_log


def run_case(args, mode, policy='enforce'):
    root = args.work_dir / ('thread-probe-' + uuid.uuid4().hex)
    root.mkdir()
    instance = root/'instance'
    data = instance/'redirect-data'
    data.mkdir(parents=True)
    for name in ('local', 'roaming', 'documents', 'savedgames', 'profile'):
        (data/name).mkdir()
    env = {k:v for k,v in os.environ.items() if not k.startswith('COOP_')}
    env.update(COOP_REDIRECT_INSTANCE=str(data), COOP_MODULE_INSTANCE=str(instance),
               COOP_MODULE_POLICY='allowlist', COOP_NETWORK_POLICY=policy,
               COOP_THREAD_TRACE=mode)
    for key in ('LOCAL', 'ROAMING', 'DOCUMENTS', 'SAVEDGAMES'):
        real = root/('real-'+key.lower())
        real.mkdir()
        env['COOP_REAL_'+key] = str(real)
    steam = root/'fixture-steam'
    steam.mkdir()
    env['COOP_REDIRECT_STEAM'] = str(steam)
    for source, target in ((args.proxy, instance/'proxy.dll'), (args.probe, instance/'probe.exe'),
                           (args.probe.with_name('thread_trace_probe_module.dll'), instance/'late.dll')):
        with source.open('rb') as src, target.open('xb') as dst:
            shutil.copyfileobj(src, dst)
    child = subprocess.Popen([str(instance/'probe.exe'), str(instance/'proxy.dll'),
                              'on' if mode == '1' else 'off', str(instance/'late.dll')], cwd=instance,
                             env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    try:
        # readline alone has no timeout on Windows pipes. The owned child is
        # killed in finally on timeout; no process-name or desktop operations.
        lines = queue.Queue()
        thread = threading.Thread(target=lambda: lines.put(child.stdout.readline()), daemon=True)
        thread.start()
        line = lines.get(timeout=15)
        if mode not in ('0', '1') or policy != 'enforce':
            _, err = child.communicate(timeout=5)
            if child.returncode != 125:
                raise AssertionError(f'unsafe/invalid mode did not refuse: {child.returncode}: {err}')
            return {'fixture': str(root), 'refused': True}
        if not line:
            raise AssertionError(f'probe exited before report: {child.poll()}')
        info = json.loads(line)
        if info['pid'] != child.pid or info['failures']:
            raise AssertionError(f'probe semantics failed: {info}')
        logs = list(data.glob(f"redirect-{child.pid}-*.log"))
        if len(logs) != 1:
            raise AssertionError('one process-start-specific audit log required')
        if mode == '1':
            pid, start, address = identity_from_log(logs[0])
            if start != info['start']:
                raise AssertionError('log/probe process identity mismatch')
            reader = WindowsReader(pid, start)
            try:
                report = decode(reader.read(address), reader.read(address), pid, start)
            finally:
                reader.close()
            if not {info['main_tid'], info['worker_tid']}.issubset(report['observed_thread_ids']):
                raise AssertionError('both process threads must be independently observed')
            if not any(row['depth'] > 1 for row in report['records']):
                raise AssertionError('nested dispatch must not look like a unique frame boundary')
            results = [row['result'] for row in report['records'] if row['api']=='GetMessageA' and row['phase']=='leave']
            if not all(any(test(value) for value in results) for test in
                       (lambda x:x<0, lambda x:x==0, lambda x:x>0)):
                raise AssertionError('negative/zero/positive GetMessage results must survive trace')
            if not any(row['stack'] for row in report['records']):
                raise AssertionError('native stack capture required')
            if any(row['caller']['host_rva'] is None for row in report['records']):
                raise AssertionError('observer must trace main-executable IAT callers only')
            (root/'trace.json').write_text(json.dumps(report, indent=2))
            # A wrong creation time must refuse before process memory is read.
            try:
                wrong = WindowsReader(pid, start+1)
            except ValueError:
                pass
            else:
                wrong.close()
                raise AssertionError('PID-reuse identity mutant survived')
        else:
            if 'THREAD_TRACE\t' in logs[0].read_text(encoding='utf-16-le'):
                raise AssertionError('disabled observer published trace')
            report = {'records': []}
        child.stdin.write('\n')
        child.stdin.flush()
        _, err = child.communicate(timeout=5)
        if child.returncode:
            raise AssertionError(f'probe failed: {child.returncode}: {err}')
        return {'fixture': str(root), 'mode': mode, 'records': len(report['records']), 'semantics': info}
    finally:
        if child.poll() is None:
            child.kill()
        child.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proxy', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--case', choices=('all','enabled','network'), default='all')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('native Windows Python required (owns process handles)')
    args.work_dir = args.work_dir.resolve()
    forbidden = {'kojimaproductions', 'userdata', 'steamapps', 'windows', 'program files', 'program files (x86)'}
    if (not args.work_dir.is_dir() or args.work_dir.drive.startswith('\\\\') or
        any(part.casefold() in forbidden for part in args.work_dir.parts)):
        parser.error('use an existing disposable drive folder outside game/save/system roots')
    for parent in (args.work_dir, *args.work_dir.parents):
        if parent.is_symlink() or getattr(parent.stat(), 'st_file_attributes', 0) & 0x400:
            parser.error('reparse/symlink workspace refused')
    cases = ([run_case(args, '1')] if args.case=='enabled' else
             [run_case(args, '1', 'log-only')] if args.case=='network' else
             [run_case(args, '0'), run_case(args, '1'), run_case(args, 'invalid'), run_case(args, '1', 'log-only')])
    print(json.dumps({'ok': True, 'cases': cases}, indent=2))


if __name__ == '__main__':
    main()
