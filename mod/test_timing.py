"""Require failures at the same timing oracles, not crashes."""
import subprocess
from pathlib import Path
exe = str(Path(__file__).resolve().parent/'build/timing-test')
for mode, oracle in (('', ''), ('legacy', 'timing_error_bounds'), ('slow-ping', 'early_ping_retry')):
    r = subprocess.run([exe]+([mode] if mode else []), capture_output=True, text=True, timeout=20)
    print(r.stdout, end='')
    if not mode:
        assert r.returncode == 0, r.stderr
    else:
        assert r.returncode == 1 and 'FAIL '+oracle in r.stderr, r.stderr
        print('KILLED timing mutant', mode, oracle)
