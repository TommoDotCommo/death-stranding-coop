#!/usr/bin/env python3
"""A mutant must fail its named oracle, not merely crash or return nonzero."""
import subprocess
import sys
import time
start = time.monotonic()
exe = sys.argv[1]
clean = subprocess.run([exe], text=True, capture_output=True)
print(clean.stdout, end='')
if clean.returncode:
    print(clean.stderr)
    sys.exit(1)
mutants = ['version', 'range', 'teleport', 'duplicate', 'delay', 'adapt', 'refine', 'session', 'build',
           'magic', 'checksum', 'reserved', 'nonce', 'time', 'speed', 'floor',
           'extrapolate', 'stale', 'gate', 'timeout', 'buffer', 'rate', 'length', 'kind', 'role', 'handshake', 'ping', 'pingHistory', 'clamp', 'finite', 'cargoOrder', 'cargoAck', 'cargoOnce', 'progressReady', 'progress', 'progressUnknown', 'progressFlag', 'progressPolicy', 'vehicle', 'vehicleLerp']
bad = 0
for mutant in mutants:
    result = subprocess.run([exe, mutant], text=True, capture_output=True)
    caught = result.returncode == 1 and f'FAIL {mutant}\n' in result.stderr
    print(f'{"KILLED" if caught else "SURVIVED"} {mutant}')
    if not caught:
        print(result.stdout + result.stderr)
        bad += 1
print(f'mutants={len(mutants)} killed={len(mutants)-bad} suite_runtime={time.monotonic()-start:.3f}s')
sys.exit(bool(bad))
