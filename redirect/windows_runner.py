"""Windows-only subprocess bridge. A game remains suspended until PID registration.

No shell, process-name kills or child tree kills. Stop checks image AND creation
FILETIME on an opened process handle, avoiding PID reuse between check and kill.
"""
import ctypes as c
from ctypes import wintypes as w
import json
import os
import subprocess
import sys


def kernel():
    k = c.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
    k.CloseHandle.argtypes = [w.HANDLE]
    k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]; k.WaitForSingleObject.restype = w.DWORD
    k.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    k.GetProcessTimes.argtypes = [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)]
    k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    return k


def identity(k, handle):
    created, exited, system, user = (w.FILETIME() for _ in range(4))
    if not k.GetProcessTimes(handle, c.byref(created), c.byref(exited), c.byref(system), c.byref(user)):
        raise c.WinError(c.get_last_error())
    size = w.DWORD(32768); image = c.create_unicode_buffer(size.value)
    if not k.QueryFullProcessImageNameW(handle, 0, image, c.byref(size)):
        raise c.WinError(c.get_last_error())
    return {'created': created.dwHighDateTime << 32 | created.dwLowDateTime, 'image': image.value}


def main():
    mode = sys.argv[1]
    config = json.loads(open(sys.argv[2], encoding='utf-8').read())
    if mode == 'probe':
        os.environ.update(config['env'])
        r = subprocess.run([config['exe'], config['dll']], capture_output=True, text=True, timeout=30)
        if r.returncode or 'module_probe' not in config:
            print(r.stdout, end=''); sys.stderr.write(r.stderr); return r.returncode
        report = json.loads(r.stdout)
        reports = []
        system = os.path.join(os.environ['SystemRoot'], 'System32', 'version.dll')
        for policy in ('strict', 'allowlist', 'log-only'):
            for kind, path in (('system', system), ('instance', config['module_probe'])):
                environment = dict(os.environ, COOP_MODULE_POLICY=policy)
                late = subprocess.run([config['exe'], config['dll'], '--late', path], env=environment,
                                      capture_output=True, text=True, timeout=20)
                reports.append({'policy': policy, 'kind': kind, 'returncode': late.returncode,
                                'report': json.loads(late.stdout) if late.returncode == 0 else None,
                                'stderr': late.stderr})
        report['module_tests'] = reports
        print(json.dumps(report)); return 0
    k = kernel()
    if mode == 'stop':
        h = k.OpenProcess(0x1000 | 0x100000 | 1, False, config['pid'])
        if not h: raise c.WinError(c.get_last_error())
        try:
            actual = identity(k, h)
            if actual['created'] != config['created'] or actual['image'].casefold() != config['image'].casefold():
                raise ValueError('PID identity mismatch; refusing kill')
            if not k.TerminateProcess(h, 0): raise c.WinError(c.get_last_error())
            if k.WaitForSingleObject(h, 15000) != 0: raise ValueError('process did not exit')
        finally: k.CloseHandle(h)
        print('{"ok":true}'); return 0
    if mode != 'start': raise ValueError('unknown mode')
    # subprocess retains the process handle, but not the suspended primary thread
    # handle, so use CreateProcessW directly.
    class SI(c.Structure):
        _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
                    ('x', w.DWORD), ('y', w.DWORD), ('xs', w.DWORD), ('ys', w.DWORD), ('xc', w.DWORD), ('yc', w.DWORD),
                    ('fill', w.DWORD), ('flags', w.DWORD), ('show', w.WORD), ('n', w.WORD), ('bytes', c.c_void_p),
                    ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]
    class PI(c.Structure):
        _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]
    k.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(SI), c.POINTER(PI)]
    k.ResumeThread.argtypes = [w.HANDLE]; k.ResumeThread.restype = w.DWORD
    si, pi = SI(), PI(); si.cb = c.sizeof(si); si.flags = 1; si.show = 4  # SW_SHOWNOACTIVATE
    os.environ.update(config['env'])
    command = c.create_unicode_buffer(subprocess.list2cmdline([config['exe']]))
    if not k.CreateProcessW(config['exe'], command, None, None, False, 4, None, config['cwd'], c.byref(si), c.byref(pi)):
        raise c.WinError(c.get_last_error())
    resumed = False
    try:
        record = {'pid': pi.pid, **identity(k, pi.process)}
        print(json.dumps(record), flush=True)
        if sys.stdin.readline().strip() != 'resume': raise ValueError('registration aborted')
        if k.ResumeThread(pi.thread) == 0xffffffff: raise c.WinError(c.get_last_error())
        resumed = True; print('{"resumed":true}', flush=True)
    finally:
        if not resumed: k.TerminateProcess(pi.process, 125)
        k.CloseHandle(pi.thread); k.CloseHandle(pi.process)
    return 0


if __name__ == '__main__':
    sys.exit(main())
