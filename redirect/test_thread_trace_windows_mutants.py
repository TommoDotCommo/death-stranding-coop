#!/usr/bin/env python3
"""WSL cross-builds: each broken scope/network guard must fail the Windows probe.

Uses only new game-free fixture processes; never deploy mutants into a game copy.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
MUTANTS = (
    ('scope', 'if(trace_enabled && module==GetModuleHandleW(nullptr))',
     'if(trace_enabled)', 'enabled', 'probe semantics failed'),
    ('network', 'if(net.mode!=redirect::NetMode::enforce) fatal("redirect: thread trace requires enforced network policy\\n");',
     'if(false) fatal("redirect: thread trace requires enforced network policy\\n");',
     'network', 'unsafe/invalid mode did not refuse'),
)


def windows(path):
    return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows-python', required=True)
    parser.add_argument('--work-dir', required=True, help='native Windows disposable drive folder')
    args = parser.parse_args()
    destination = ROOT/'build/trace-mutants'
    destination.mkdir(exist_ok=True)
    original = (ROOT/'redirect.cpp').read_text()
    for name, before, after, case, oracle in MUTANTS:
        if original.count(before) != 1:
            raise AssertionError(f'{name}: mutation anchor changed')
        source = destination/(name+'.cpp')
        source.write_text(original.replace(before, after))
        artifact = destination/(name+'.dll')
        production = ['path_map.cpp', 'module_policy.cpp', 'audit_throttle.cpp',
                      'objects.cpp', 'network_policy.cpp', 'proxy.cpp', '../mod/config.cpp']
        subprocess.run(['x86_64-w64-mingw32-g++', '-std=c++17', '-O2', '-D_WIN32_WINNT=0x0602',
            '-static-libgcc', '-static-libstdc++', '-shared', '-I'+str(ROOT), str(source),
            *(str(ROOT/name) for name in production), str(ROOT/'d3dcompiler_47.def'),
            '-o', str(artifact), '-lshell32', '-lole32', '-luuid', '-lws2_32', '-lwinhttp', '-lbcrypt', '-lwinmm'], check=True)
        result = subprocess.run([args.windows_python, windows(ROOT/'test_thread_trace_windows.py'),
            '--proxy', windows(artifact), '--probe', windows(ROOT/'build/thread_trace_probe.exe'),
            '--work-dir', args.work_dir, '--case', case], capture_output=True, text=True, timeout=40)
        (destination/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode != 1 or oracle not in result.stdout+result.stderr:
            raise AssertionError(f'{name}: survived or failed outside oracle: {result.returncode}: {result.stderr}')
        print('KILLED Windows thread observer', name, flush=True)


if __name__ == '__main__':
    main()
